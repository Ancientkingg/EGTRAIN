// Stand-in for an installed application that needs a DLL next to it. When the
// library is missing, the Windows loader ends the process before main runs.
extern "C" __declspec(dllimport) int egtrain_probe_value();

int main() {
	return egtrain_probe_value() == 42 ? 0 : 1;
}
