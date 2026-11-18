#include "Box3DReplicationSettings.h"

UBox3DReplicationSettings::UBox3DReplicationSettings()
{
	DistanceRatesHz.Add(1500.0f, 60.0f);
	DistanceRatesHz.Add(2500.0f, 45.0f);
	DistanceRatesHz.Add(4000.0f, 30.0f);
	DistanceRatesHz.Add(7000.0f, 15.0f);
	DistanceRatesHz.Add(10000.0f, 5.0f);
}
