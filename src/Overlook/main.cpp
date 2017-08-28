#include "Overlook.h"

using namespace Overlook;


#define IMAGECLASS OverlookImg
#define IMAGEFILE <Overlook/Overlook.iml>
#include <Draw/iml_source.h>


GUI_APP_MAIN {
	LOG(GetAmpDevices());
	//TestCompatAMP(); return;
	
	const Vector<String>& args = CommandLine();
	for(int i = 1; i < args.GetCount(); i+=2) {
		const String& s = args[i-1];
		if (s == "-addr") {
			arg_addr = args[i];
		}
		else if (s == "-port") {
			arg_port = ScanInt(args[i]);
		}
	}
	
	System& sys = GetSystem();
	sys.Init();
	sys.Start();
	{
		::Overlook::Overlook ol;
		ol.Init();
		ol.Run();
	}
	sys.Stop();
	
	{
		TopWindow tw;
		tw.Icon(OverlookImg::icon());
		tw.Title("Saving agent group");
		Label lbl;
		lbl.SetLabel("Saving... please wait.");
		lbl.SetAlign(ALIGN_CENTER);
		tw.SetRect(0,0, 320, 60);
		tw.Add(lbl.SizePos());
		Thread::Start([&]() {
			AgentGroup& ag = sys.GetAgentGroup();
			ag.StoreThis();
			PostCallback(callback(&tw, &TopWindow::Close));
			PostCallback(callback(&tw, &TopWindow::Close));
		});
		tw.Run();
	}
	
	Thread::ShutdownThreads();
}

