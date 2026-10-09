#ifndef Data_NumberGenerator_hpp_
#define Data_NumberGenerator_hpp_

class NumberGenerator {
public:
	// The state advances by multiplying with kIa modulo kIm. kIq is kIm / kIa and kIr is kIm % kIa, so that the
	// product needs no larger type. The next result is taken from a table of kNtab entries, from the entry that
	// the previous result selects through kNdiv. getUniformFloat limits the draw on the unit interval to kRnmx
	// before it scales it to the requested interval.
	static constexpr int kIa = 16807;
	static constexpr int kIm = 2147483647;
	static constexpr double kAm = 1.0 / kIm;
	static constexpr int kIq = 127773;
	static constexpr int kIr = 2836;
	static constexpr int kNtab = 32;
	static constexpr int kNdiv = 1 + (kIm - 1) / kNtab;
	static constexpr double kEps = 1.2e-7;
	static constexpr double kRnmx = 1.0 - kEps;

	// Seeds from 1 to kMaxRandomSeed are valid. 0 and kIm lead to the all-zero state, in which
	// getGaussianFloat does not return.
	explicit NumberGenerator(unsigned long inSeed);

	int operator()(unsigned long inValue) { return getUniformInteger(0, inValue - 1); }

	bool getUniformBool(void) { return getUniformInteger(0, 1); }
	int getUniformInteger(int inFirst, int inLast);
	double getUniformFloat(double inFirst = 0., double inLast = 1.0);
	double getGaussianFloat(double inMean = 0, double inStdDev = 1);

private:
	long iy;
	long iv[kNtab];
	long idum;

	int iset;
	double gset;
};

constexpr unsigned long kMaxRandomSeed = NumberGenerator::kIm - 1;

// The generator that drives the passenger sampling of the prepared run.
NumberGenerator& runNumberGenerator();
// Restarts the run generator from inSeed. Scene preparation calls it, so every run starts from the same state.
void seedRunNumberGenerator(unsigned long inSeed);

#endif
