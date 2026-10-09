#include "simulation/NumberGenerator.h"
#include "simulation/InitialParameters.h"
#include <math.h>

NumberGenerator::NumberGenerator(unsigned long inSeed) {
	idum = inSeed;
	for (int j = kNtab + 7; j >= 0; --j) {
		long k = idum / kIq;
		idum = kIa * (idum - k * kIq) - kIr * k;
		if (idum < 0)
			idum += kIm;
		if (j < kNtab)
			iv[j] = idum;
	}
	iy = iv[0];
	iset = 0;
	gset = 0.0;
}

int NumberGenerator::getUniformInteger(int inFirst,
	int inLast) {
	int lNumber = (int)(inFirst + (inLast - inFirst + 1) * getUniformFloat());
	if (lNumber > inLast)
		lNumber = inLast;
	return lNumber;
}

double NumberGenerator::getUniformFloat(double inFirst,
	double inLast) {
	double lTmp, lNumber;

	long k = idum / kIq;
	idum = kIa * (idum - k * kIq) - kIr * k;
	if (idum < 0)
		idum += kIm;
	int j = (int)iy / kNdiv;
	iy = iv[j];
	iv[j] = idum;
	if ((lTmp = kAm * iy) > kRnmx)
		lNumber = kRnmx;
	else
		lNumber = lTmp;
	return inFirst + (inLast - inFirst) * lNumber;
}

double NumberGenerator::getGaussianFloat(double inMean,
	double inStdDev) {
	double fac, rsq, v1, v2;

	if (iset == 0) {
		do {
			v1 = 2.0 * getUniformFloat() - 1.0;
			v2 = 2.0 * getUniformFloat() - 1.0;
			rsq = v1 * v1 + v2 * v2;
		} while (rsq >= 1.0 || rsq == 0);
		fac = sqrt(-2.0 * log(rsq) / rsq);
		gset = v1 * fac;
		iset = 1;
		return v2 * fac * inStdDev + inMean;
	} else {
		iset = 0;
		return gset * inStdDev + inMean;
	}
}

NumberGenerator& runNumberGenerator() {
	static NumberGenerator generator(kDefaultRandomSeed);
	return generator;
}

void seedRunNumberGenerator(unsigned long inSeed) {
	runNumberGenerator() = NumberGenerator(inSeed);
}
