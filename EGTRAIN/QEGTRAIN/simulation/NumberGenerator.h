#ifndef Data_NumberGenerator_hpp_
#define Data_NumberGenerator_hpp_

/// The random draws of a run. Each SimulationRun has one, which the operations builder seeds from initial_variables.randomSeed,
/// so equal seeds give equal draws.
///
/// Park-Miller minimal standard generator with a Bays-Durham shuffle table. kIq and kIr keep the product within 32 bits.
class NumberGenerator {
public:
	static constexpr int kIa = 16807;					///< Multiplier of the state.
	static constexpr int kIm = 2147483647;				///< Modulus of the state, 2^31 - 1.
	static constexpr double kAm = 1.0 / kIm;			///< Turns a state into a number on the unit interval.
	static constexpr int kIq = 127773;					///< kIm / kIa.
	static constexpr int kIr = 2836;					///< kIm % kIa.
	static constexpr int kNtab = 32;					///< Number of entries of the table.
	static constexpr int kNdiv = 1 + (kIm - 1) / kNtab; ///< Turns the previous result into an index of the table.
	static constexpr double kEps = 1.2e-7;				///< Keeps a draw on the unit interval below 1.
	static constexpr double kRnmx = 1.0 - kEps;			///< Largest draw on the unit interval.

	/// Seeds from 1 to kMaxRandomSeed are valid. 0 and kIm lead to the all-zero state, in which
	/// getGaussianFloat does not return.
	explicit NumberGenerator(unsigned long inSeed);

	/// A uniform integer from 0 to inValue - 1.
	int operator()(unsigned long inValue) { return getUniformInteger(0, inValue - 1); }

	/// True or false with equal chance.
	bool getUniformBool(void) { return getUniformInteger(0, 1); }
	/// A uniform integer from inFirst to inLast, both included.
	int getUniformInteger(int inFirst, int inLast);
	/// A uniform number from inFirst up to, but not including, inLast when inLast > inFirst; inFirst when the two are equal.
	double getUniformFloat(double inFirst = 0., double inLast = 1.0);
	/// A normal draw with mean inMean and standard deviation inStdDev. Draws are made in pairs: every second call
	/// returns the value kept from the call before.
	double getGaussianFloat(double inMean = 0, double inStdDev = 1);

private:
	long iy;
	long iv[kNtab];
	long idum;

	int iset;
	double gset;
};

/// The largest valid seed.
constexpr unsigned long kMaxRandomSeed = NumberGenerator::kIm - 1;

#endif
