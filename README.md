<div align="center">
  <h1>Box3D Unreal</h1>
  <!-- Replace the placeholder below with <img src="docs/images/header.png" alt="Box3D shapes falling onto a platform in Unreal Engine"> when the banner is ready. -->
  <p><em>Header image coming soon</em></p>
  <p>Simulate rigid bodies, joints, and breakable objects in Unreal Engine with Erin Catto’s Box3D physics engine.</p>
  <p>
    <a href="#quick-start"><img src="https://img.shields.io/badge/Unreal_Engine-5.7-20252B?logo=unrealengine" alt="Unreal Engine 5.7"></a>
    <a href="#quick-start"><img src="https://img.shields.io/badge/Setup-Windows-0067B8" alt="Windows setup"></a>
    <a href="#quick-start"><img src="https://img.shields.io/badge/Project-C%2B%2B-00599C?logo=cplusplus" alt="C++ project"></a>
    <a href="https://github.com/erincatto/box3d"><img src="https://img.shields.io/badge/Box3D-v0.1.0-2B7565" alt="Box3D v0.1.0"></a>
    <a href="Box3DUnreal.uplugin"><img src="https://img.shields.io/badge/Plugin-beta-B55A14" alt="Plugin beta"></a>
  </p>
  <p>
    <a href="#what-youll-try"><img src="https://img.shields.io/badge/Blueprint-queries-2457A6" alt="Blueprint queries"></a>
    <a href="#run-your-first-simulation"><img src="https://img.shields.io/badge/Debug-drawing-5865A8" alt="Debug drawing"></a>
    <a href="docs/DESTRUCTION.md"><img src="https://img.shields.io/badge/Physics-destruction-A13A42" alt="Physics destruction"></a>
    <a href="LICENSE"><img src="https://img.shields.io/badge/Plugin_license-MIT-2E744C" alt="Plugin license MIT"></a>
  </p>
</div>

## What you’ll try

Start by dropping a group of shapes onto a platform you can see in the editor. Each shape is a **rigid body**, an object that can move, rotate, and collide while keeping its shape.

- **Bodies and joints** let you build moving props and connect them with constraints such as hinges or springs.
- **Blueprint queries** let you cast a ray or a shape through the Box3D world, or check which bodies overlap a volume.
- **Destruction** lets you fracture a mesh into pieces and simulate how those pieces break apart. See the [destruction guide](docs/DESTRUCTION.md).

## Quick start

### 1. Install the requirements

- [Unreal Engine 5.7](https://www.unrealengine.com/download)
- [Git for Windows](https://git-scm.com/download/win)
- [Visual Studio 2022](https://learn.microsoft.com/en-us/visualstudio/gamedev/unreal/get-started/vs-tools-unreal-install) with the **Game development with C++** workload, including MSVC and a Windows SDK
- An Unreal Engine 5.7 **C++ project**. To start from scratch, create a **Games → Blank** project and choose **C++** in the project settings before creating it.

### 2. Clone the plugin into your project

Close Unreal Editor. Open PowerShell in the folder containing your project's `.uproject` file and run:

```powershell
git clone https://github.com/ArtemIyX/Box3DUnreal.git Plugins/Box3DUnreal
```

Check that `Plugins/Box3DUnreal/Box3DUnreal.uplugin` now exists. The plugin includes the Box3D source, which Unreal Build Tool compiles with your project.

### 3. Enable and build

Open your `.uproject` file with Unreal Engine 5.7. If Unreal prompts you to rebuild missing modules, select **Yes**. In **Edit → Plugins**, search for **Box3D Unreal** and enable it if needed. Restart the editor when prompted.

## Run your first simulation

1. Open a basic level or choose **File → New Level → Basic**. In the editor viewport, position the camera about five meters above the floor and face an open area. Hold the right mouse button and use **E** to move up.
2. In the dropdown next to **Play**, set **Spawn Player At** to **Current Camera Location**, then click **Play**. Press the console key (usually `~` on a US keyboard) to open the in-game console.
3. Enter this command, then close the console and look slightly down in front of you.

   ```text
   box3d.SmokeActors 20
   ```

   Twenty cubes, spheres, and cylinders should fall onto a slab and settle. The command places its own slab three meters below your view. Starting above the level floor keeps the demo in sight.

| Console command | What it does |
| --- | --- |
| `box3d.DebugDraw 1` | Shows wireframes for the Box3D collision shapes. Use `box3d.DebugDraw 0` to hide them. |
| `box3d.SmokeActors 0` | Removes the smoke-test actors and their slab. |

## Explore further

| Module | Where to look |
| --- | --- |
| [Box3DCore](Source/Box3DCore/) | The bundled Box3D engine. |
| [Box3DRuntime](Source/Box3DRuntime/) | Unreal-facing bodies, joints, queries, and simulation settings. |
| [Box3DEditor](Source/Box3DEditor/) | Tools for baking level collision ahead of time. |
| [Box3DTests](Source/Box3DTests/) | Unreal automation tests for the integration. |

- **Run the tests.** Enter `Automation RunTests Box3DUnreal` in Unreal's console. See the [testing guide](docs/TESTING.md).
- **Prepare level collision for packaged builds.** See the [baked collision guide](docs/BAKED_COLLISION.md).
- **Try fracture, debris, and deformation.** See the [destruction guide](docs/DESTRUCTION.md).
- **Explore snapshots, prediction, and rollback.** See the [networking guide](docs/NETWORKING.md).
- **See how the integration works or contribute.** Read the [design notes](docs/DESIGN.md) and the [contributing guide](CONTRIBUTING.md).

## Troubleshooting

| If you see this | Try this |
| --- | --- |
| The plugin folder is empty or the plugin is missing from the list. | Confirm the `.uplugin` file is directly under `Plugins/Box3DUnreal/`. If your project includes the plugin as a Git submodule, run `git submodule update --init --recursive` from the project root. |
| Unreal can't build the modules. | Check that your project opens with UE 5.7 and that the Visual Studio C++ workload, MSVC, and Windows SDK are installed. Close Unreal, right-click the `.uproject`, choose **Generate Visual Studio project files**, and build the **Development Editor** target for **Win64** in Visual Studio. |
| The console doesn't recognize `box3d.SmokeActors`. | Check that **Box3D Unreal** is enabled, restart the editor, and retry from the in-game console after clicking **Play**. |

## License and credits

- Box3D Unreal is available under the [MIT license](LICENSE).
- The bundled [Box3D](https://github.com/erincatto/box3d) engine is by Erin Catto and carries its own [MIT license](Source/Box3DCore/box3d.LICENSE).
- Baked collision and snapshot/rollback work builds on [Antonio Lattanzio’s Box3DUnreal](https://github.com/alattanzio/Box3DUnreal).
