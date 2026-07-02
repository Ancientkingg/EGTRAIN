#include "NumberGenerator.h"
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <iostream>
#include <fstream>

using namespace std;

NumberGenerator rand1;

NumberGenerator::NumberGenerator(void) {
	ifstream lStream("rand1.seed");
	if (lStream.good())
		lStream >> idum;
	else
		idum = time(0);
	for (int j = NTAB + 7; j >= 0; --j) {
		long k = idum / IQ;
		idum = IA * (idum - k * IQ) - IR * k;
		if (idum < 0)
			idum += IM;
		if (j < NTAB)
			iv[j] = idum;
	}
	iy = iv[0];
	iset = 0;
	gset = 0.0;
}

NumberGenerator::NumberGenerator(unsigned long inSeed) {
	idum = inSeed;
	for (int j = NTAB + 7; j >= 0; --j) {
		long k = idum / IQ;
		idum = IA * (idum - k * IQ) - IR * k;
		if (idum < 0)
			idum += IM;
		if (j < NTAB)
			iv[j] = idum;
	}
	iy = iv[0];
	iset = 0;
	gset = 0.0;
}

NumberGenerator::~NumberGenerator(void) {
	ofstream lStream("rand1.seed");
	if (!lStream.good())
		cerr << "Unable to create file \"rand1.seed\"!" << endl;
	else
		lStream << idum;
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

	long k = idum / IQ;
	idum = IA * (idum - k * IQ) - IR * k;
	if (idum < 0)
		idum += IM;
	int j = (int)iy / NDIV;
	iy = iv[j];
	iv[j] = idum;
	if ((lTmp = AM * iy) > RNMX)
		lNumber = RNMX;
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

// Function to generate random number in a range between Min and Max
double NumberGenerator::generateRandomNumberInRange(double Min, double Max) {
	// generate random number in range Min - Max using std function rand
	double randomNum = -1;

	randomNum = Min + ((double)rand() / ((double)RAND_MAX - 0)) * (Max - Min);
	// return generated number
	return randomNum;
}