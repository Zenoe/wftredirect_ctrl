Step 1: Project Setup in VS2022

Open Visual Studio 2022
File → New → Project
Search for "Kernel Mode Driver, Empty (KMDF)" → select it → Next
Name it WfpHello, location of your choice → Create
Right-click the project → Properties → confirm:

Configuration: All Configurations
Platform: x64
Driver Settings → Target OS Version: Windows 10 or later
Driver Settings → Target Platform: Desktop

Linker → Input → Additional Dependencies, add:
fwpkclnt.lib
fwpuclnt.lib
ndis.lib

Properties: Inf2Cat

Run Inf2Cat: Yes


DriverEntry
  └── IoCreateDevice            ← WFP requires a device object
  └── WfpHelloRegister()
        ├── FwpmEngineOpen0     ← connect to WFP engine
        ├── FwpsCalloutRegister3 ← register kernel callout (gets numeric ID)
        ├── FwpmCalloutAdd0     ← register management callout
        ├── FwpmSubLayerAdd0    ← create a sublayer
        └── FwpmFilterAdd0      ← attach filter → callout at ALE_AUTH_CONNECT_V4

WfpHelloClassify()              ← fires on EVERY outbound TCP connect
  └── reads local/remote IP+port from FWPS_INCOMING_VALUES
  └── DbgPrintEx(...)           ← visible in DebugView
  └── FWP_ACTION_PERMIT         ← never blocks, only observes

The key WFP concept: kernel callout (FwpsCalloutRegister3) + management filter (FwpmFilterAdd0) work as a pair — the filter tells WFP which traffic to intercept, the callout is what runs when it matches.