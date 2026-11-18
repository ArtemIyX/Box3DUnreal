#include "Box3DAsyncSubsystemImpl.h"
#include "Box3DAsyncEventOrdering.h"

namespace
{
	bool MatchesEvent(const FBox3DAsyncEventFilter& Filter, const FBox3DAsyncEvent& Event)
	{
		const FBox3DAsyncBodyId Body = Filter.BodyHandle.Id ? Filter.BodyHandle.Id : Filter.Body;
		const FBox3DAsyncShapeId Shape = Filter.ShapeHandle.Id ? Filter.ShapeHandle.Id : Filter.Shape;
		if (Body && Body != Event.BodyA && Body != Event.BodyB) return false;
		if (Shape && Shape != Event.ShapeA && Shape != Event.ShapeB) return false;
		switch (Event.Kind)
		{
		case EBox3DAsyncEventKind::ContactBegin:
		case EBox3DAsyncEventKind::ContactEnd: return Filter.bContacts;
		case EBox3DAsyncEventKind::SensorEnter:
		case EBox3DAsyncEventKind::SensorExit: return Filter.bSensors;
		case EBox3DAsyncEventKind::Impact: return Filter.bImpacts && Event.ApproachSpeed >= Filter.MinimumImpactSpeed;
		}
		return false;
	}

	void FilterBatch(const FBox3DAsyncEventFilter& Filter, const FBox3DAsyncEventBatch& In, FBox3DAsyncEventBatch& Out)
	{
		Out.World = In.World;
		Out.Step = In.Step;
		Out.Sequence = In.Sequence;
		Out.Sample = In.Sample;
		for (const FBox3DAsyncEvent& Event : In.Events)
			if (MatchesEvent(Filter, Event)) Out.Events.Add(Event);
	}

	TMap<Box3DAsyncPrivate::FEventPairKey, FBox3DAsyncEvent> BodyPairs(const TArray<FBox3DAsyncEvent>& Shapes)
	{
		TMap<Box3DAsyncPrivate::FEventPairKey, FBox3DAsyncEvent> Result;
		for (FBox3DAsyncEvent Event : Shapes)
		{
			const auto Key = Box3DAsyncPrivate::BodyPair(Event);
			if (Event.Kind == EBox3DAsyncEventKind::ContactBegin && Event.BodyA > Event.BodyB)
			{
				Swap(Event.BodyA, Event.BodyB);
				Swap(Event.BodyHandleA, Event.BodyHandleB);
			}
			Event.ShapeA = Event.ShapeB = 0;
			Event.ShapeHandleA = {}; Event.ShapeHandleB = {};
			Event.ContactIndex = Event.ContactWorld = Event.ContactGeneration = 0;
			Result.FindOrAdd(Key, Event);
		}
		return Result;
	}

	void AggregateBodies(TArray<FBox3DAsyncEvent>& Active, FBox3DAsyncEventBatch& Batch, bool bBaseline)
	{
		const auto Previous = BodyPairs(Active);
		TMap<Box3DAsyncPrivate::FEventPairKey, FBox3DAsyncEvent> Shapes;
		if (!bBaseline)
			for (const FBox3DAsyncEvent& Event : Active) Shapes.Add(Box3DAsyncPrivate::ShapePair(Event), Event);
		TSet<Box3DAsyncPrivate::FEventPairKey> RemovedBodies;
		TArray<FBox3DAsyncEvent> Impacts;
		for (const FBox3DAsyncEvent& Event : Batch.Events)
		{
			if (Event.Kind == EBox3DAsyncEventKind::Impact) { Impacts.Add(Event); continue; }
			const auto Key = Box3DAsyncPrivate::ShapePair(Event);
			if (Event.Kind == EBox3DAsyncEventKind::ContactBegin || Event.Kind == EBox3DAsyncEventKind::SensorEnter)
				Shapes.FindOrAdd(Key, Event);
			else
			{
				Shapes.Remove(Key);
				if (Event.EndCause == EBox3DAsyncEventEndCause::Removed) RemovedBodies.Add(Box3DAsyncPrivate::BodyPair(Event));
			}
		}
		Active.Reset();
		Shapes.GenerateValueArray(Active);
		Active.Sort(Box3DAsyncPrivate::EventLess);
		const auto Current = BodyPairs(Active);
		Batch.Events.Reset();
		if (!bBaseline)
		{
			for (const auto& Pair : Previous)
			{
				if (Current.Contains(Pair.Key)) continue;
				FBox3DAsyncEvent Event = Pair.Value;
				Event.Kind = Event.Kind == EBox3DAsyncEventKind::ContactBegin ? EBox3DAsyncEventKind::ContactEnd : EBox3DAsyncEventKind::SensorExit;
				if (RemovedBodies.Contains(Pair.Key)) Event.EndCause = EBox3DAsyncEventEndCause::Removed;
				Batch.Events.Add(Event);
			}
		}
		for (const auto& Pair : Current)
			if (bBaseline || !Previous.Contains(Pair.Key)) Batch.Events.Add(Pair.Value);
		Batch.Events.Append(MoveTemp(Impacts));
		Batch.Events.Sort(Box3DAsyncPrivate::EventLess);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
void Box3DAsyncPrivate::AggregateBodiesForTest(TArray<FBox3DAsyncEvent>& Active, FBox3DAsyncEventBatch& Batch, bool bBaseline)
{
	AggregateBodies(Active, Batch, bBaseline);
}
#endif

bool UBox3DAsyncSubsystem::SubscribeEvents(const FBox3DAsyncEventFilter& Filter, FBox3DAsyncEventSubscription& OutSubscription)
{
	check(IsInGameThread());
	OutSubscription = {};
	if (!Impl.IsValid() || !FMath::IsFinite(Filter.MinimumImpactSpeed) || Filter.MinimumImpactSpeed < 0.0f
		|| (Filter.BodyHandle.Id && (Filter.Body || Filter.BodyHandle.World != Impl->QueryWorld))
		|| (Filter.ShapeHandle.Id && (Filter.Shape || Filter.ShapeHandle.World != Impl->QueryWorld))
		|| (!Filter.BodyHandle.Id && Filter.BodyHandle.World.IsValid())
		|| (!Filter.ShapeHandle.Id && Filter.ShapeHandle.World.IsValid())) return false;
	FScopeLock Lock(&Impl->QueueMutex);
	if (!Impl->bAccepting || Impl->NextEventSubscriberId == 0 || Impl->EventSubscribers.Num() >= 32) return false;
	OutSubscription.World = Impl->QueryWorld;
	OutSubscription.Id = Impl->NextEventSubscriberId++;
	Impl->EventSubscribers.Add(OutSubscription.Id).Filter = Filter;
	return true;
}

bool UBox3DAsyncSubsystem::SubscribeEvents(const FBox3DAsyncEventFilter& Filter, UObject* Owner,
	TFunction<void(const FBox3DAsyncEventDelivery&)> Callback, bool bNotifyOnClose,
	FBox3DAsyncEventSubscription& OutSubscription)
{
	check(IsInGameThread());
	OutSubscription = {};
	if (!IsValid(Owner) || !Callback || !SubscribeEvents(Filter, OutSubscription)) return false;
	FScopeLock Lock(&Impl->QueueMutex);
	FImpl::FEventSubscriber& Subscriber = Impl->EventSubscribers.FindChecked(OutSubscription.Id);
	Subscriber.Owner = Owner;
	Subscriber.Callback = MoveTemp(Callback);
	Subscriber.bNotifyOnClose = bNotifyOnClose;
	return true;
}

void UBox3DAsyncSubsystem::UnsubscribeEvents(FBox3DAsyncEventSubscription Subscription)
{
	check(IsInGameThread());
	if (!Impl.IsValid() || Subscription.World != Impl->QueryWorld) return;
	FImpl::FEventSubscriber Retired;
	{
		FScopeLock Lock(&Impl->QueueMutex);
		if (FImpl::FEventSubscriber* Existing = Impl->EventSubscribers.Find(Subscription.Id))
		{
			Retired = MoveTemp(*Existing);
			Impl->EventSubscribers.Remove(Subscription.Id);
		}
	}
}

void UBox3DAsyncSubsystem::PumpEventSubscriptions()
{
	check(IsInGameThread());
	if (!Impl.IsValid() || Impl->bPumpingEvents || Impl->bClosingEvents) return;
	FImpl* const InitialImpl = Impl.Get();
	Impl->bPumpingEvents = true;
	TArray<uint64> Callbacks;
	TArray<FImpl::FEventSubscriber> Retired;
	{
		FScopeLock Lock(&Impl->QueueMutex);
		for (FBox3DAsyncEventBatch& Batch : Impl->EventIngress) Impl->EventHistory.Add(MoveTemp(Batch));
		Impl->EventIngress.Reset();
		// Eviction is per-history, so a slow subscriber will observe a missing
		// sequence and resync without pinning storage for the other subscribers.
		while (!Impl->EventHistory.IsEmpty() && (Impl->EventHistory.Num() > 128
			|| FImpl::EventArrayBytes(Impl->EventHistory) > FImpl::MaxEventHistoryBytes))
			Impl->EventHistory.RemoveAt(0, 1, EAllowShrinking::Yes);
		if (Impl->EventHistory.IsEmpty()) Impl->EventHistory.Empty();
		Impl->ExchangeDiagnostics.PeakEventHistoryBatches = FMath::Max(Impl->ExchangeDiagnostics.PeakEventHistoryBatches, Impl->EventHistory.Num());
		Impl->ExchangeDiagnostics.PeakEventHistoryBytes = FMath::Max(Impl->ExchangeDiagnostics.PeakEventHistoryBytes, FImpl::EventArrayBytes(Impl->EventHistory));
		for (auto It = Impl->EventSubscribers.CreateIterator(); It; ++It)
		{
			if (It.Value().Callback && !It.Value().Owner.IsValid())
			{
				Retired.Add(MoveTemp(It.Value()));
				It.RemoveCurrent();
			}
			else if (It.Value().Callback) Callbacks.Add(It.Key());
		}
	}
	Callbacks.Sort();
	int32 Start = 0;
	while (Start < Callbacks.Num() && Callbacks[Start] <= InitialImpl->LastPumpedEventSubscriberId) ++Start;
	for (int32 Offset = 0; Offset < FMath::Min(Callbacks.Num(), FImpl::MaxEventCallbacksPerPump); ++Offset)
	{
		if (Impl.Get() != InitialImpl) break;
		const uint64 Id = Callbacks[(Start + Offset) % Callbacks.Num()];
		InitialImpl->LastPumpedEventSubscriberId = Id;
		FBox3DAsyncEventSubscription Handle{Impl->QueryWorld, Id};
		FBox3DAsyncEventDelivery Delivery;
		if (!PollEvents(Handle, Delivery) || Delivery.Status == EBox3DAsyncEventPollStatus::Pending) continue;
		TFunction<void(const FBox3DAsyncEventDelivery&)> Callback;
		TWeakObjectPtr<UObject> Owner;
		{
			FScopeLock Lock(&Impl->QueueMutex);
			if (FImpl::FEventSubscriber* Subscriber = Impl->EventSubscribers.Find(Id))
			{
				if (!Subscriber->Owner.IsValid()) continue;
				Owner = Subscriber->Owner;
				Callback = Subscriber->Callback;
			}
		}
		if (Owner.IsValid() && Callback) Callback(Delivery);
	}
	if (Impl.Get() == InitialImpl) Impl->bPumpingEvents = false;
}

void UBox3DAsyncSubsystem::CloseEventSubscriptions()
{
	check(IsInGameThread());
	if (!Impl.IsValid()) return;
	FImpl* const InitialImpl = Impl.Get();
	// Freeze normal callbacks throughout the explicit close window, including
	// when a close handler polls or otherwise re-enters the dispatcher.
	Impl->bPumpingEvents = true;
	Impl->bClosingEvents = true;
	TArray<uint64> Handles;
	{ FScopeLock Lock(&Impl->QueueMutex); Impl->EventSubscribers.GetKeys(Handles); }
	Handles.Sort();
	for (uint64 Id : Handles)
	{
		if (Impl.Get() != InitialImpl) return;
		TFunction<void(const FBox3DAsyncEventDelivery&)> Callback;
		TWeakObjectPtr<UObject> Owner;
		{
			FScopeLock Lock(&Impl->QueueMutex);
			if (const FImpl::FEventSubscriber* Subscriber = Impl->EventSubscribers.Find(Id))
				if (Subscriber->bNotifyOnClose && Subscriber->Owner.IsValid())
				{ Owner = Subscriber->Owner; Callback = Subscriber->Callback; }
		}
		if (Owner.IsValid() && Callback)
		{
			FBox3DAsyncEventDelivery Closed;
			Closed.Status = EBox3DAsyncEventPollStatus::Closed;
			Callback(Closed);
		}
	}
	if (Impl.Get() == InitialImpl)
	{
		TMap<uint64, FImpl::FEventSubscriber> Retired;
		{
			FScopeLock Lock(&Impl->QueueMutex);
			Swap(Retired, Impl->EventSubscribers);
		}
	}
}

bool UBox3DAsyncSubsystem::PollEvents(FBox3DAsyncEventSubscription Subscription, FBox3DAsyncEventDelivery& OutDelivery)
{
	check(IsInGameThread());
	OutDelivery = {};
	if (!Impl.IsValid() || Impl->bClosingEvents || Subscription.World != Impl->QueryWorld) return false;
	FImpl* const InitialImpl = Impl.Get();
	PumpEventSubscriptions();
	if (Impl.Get() != InitialImpl || Impl->bClosingEvents) return false;
	FScopeLock Lock(&Impl->QueueMutex);
	FImpl::FEventSubscriber* Subscriber = Impl->EventSubscribers.Find(Subscription.Id);
	if (!Subscriber) return false;
	auto AggregateWithinBudget = [this, Subscriber, &OutDelivery](bool bBaseline)
	{
		if (!Subscriber->Filter.bBodyPairs) return true;
		AggregateBodies(Subscriber->ActiveShapePairs, OutDelivery.Batch, bBaseline);
		uint64 ActiveBytes = 0;
		for (const auto& Pair : Impl->EventSubscribers)
			ActiveBytes += Pair.Value.ActiveShapePairs.GetAllocatedSize();
		if (ActiveBytes <= 8ull * 1024ull * 1024ull) return true;
		Subscriber->ActiveShapePairs.Empty();
		Subscriber->bNeedsBaseline = true;
		OutDelivery.Batch = {};
		OutDelivery.Status = EBox3DAsyncEventPollStatus::Unavailable;
		++Impl->ExchangeDiagnostics.EventBaselineUnavailable;
		return false;
	};
	if (Subscriber->bReportGap)
	{
		OutDelivery.Status = EBox3DAsyncEventPollStatus::NeedsResync;
		++Impl->ExchangeDiagnostics.EventResyncs;
		OutDelivery.FirstMissedSequence = Subscriber->FirstMissed;
		OutDelivery.LastMissedSequence = Subscriber->LastMissed;
		Subscriber->bReportGap = false;
		return true;
	}
	if (Subscriber->bNeedsBaseline)
	{
		if (!Impl->bEventBaselineReady)
		{
			OutDelivery.Status = EBox3DAsyncEventPollStatus::Unavailable;
			++Impl->ExchangeDiagnostics.EventBaselineUnavailable;
			return true;
		}
		OutDelivery.Status = EBox3DAsyncEventPollStatus::Baseline;
		FilterBatch(Subscriber->Filter, Impl->EventBaseline, OutDelivery.Batch);
		if (!AggregateWithinBudget(true)) return true;
		Subscriber->NextSequence = Impl->EventBaseline.Sequence + 1;
		Subscriber->bNeedsBaseline = false;
		return true;
	}
	// Filtered-out batches still cost work. Bound the scan per poll so a single
	// subscriber cannot monopolize the game-thread dispatcher under QueueMutex.
	int32 ScannedBatches = 0;
	while (Subscriber->NextSequence <= Impl->EventSequence && ScannedBatches++ < FImpl::MaxEventBatchesPerPoll)
	{
		const FBox3DAsyncEventBatch* Batch = Impl->EventHistory.FindByPredicate([Subscriber](const FBox3DAsyncEventBatch& Candidate)
			{ return Candidate.Sequence >= Subscriber->NextSequence; });
		if (!Batch || Batch->Sequence != Subscriber->NextSequence)
		{
			OutDelivery.Status = EBox3DAsyncEventPollStatus::NeedsResync;
			++Impl->ExchangeDiagnostics.EventResyncs;
			OutDelivery.FirstMissedSequence = Subscriber->NextSequence;
			OutDelivery.LastMissedSequence = Batch ? Batch->Sequence - 1 : Impl->EventSequence;
			Subscriber->bNeedsBaseline = true;
			return true;
		}
		++Subscriber->NextSequence;
		FilterBatch(Subscriber->Filter, *Batch, OutDelivery.Batch);
		if (!AggregateWithinBudget(false)) return true;
		if (!OutDelivery.Batch.Events.IsEmpty())
		{
			OutDelivery.Status = EBox3DAsyncEventPollStatus::Events;
			return true;
		}
	}
	OutDelivery.Batch = {};
	OutDelivery.Status = EBox3DAsyncEventPollStatus::Pending;
	return true;
}
