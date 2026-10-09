// Library imported by the update helper test's DLL probe.
extern "C" __declspec(dllexport) int egtrain_probe_value() {
	return 42;
}
