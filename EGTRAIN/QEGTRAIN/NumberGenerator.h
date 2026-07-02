#ifndef Data_NumberGenerator_hpp_
#define Data_NumberGenerator_hpp_

#define IA 16807
#define IM 2147483647
#define AM (1.0 / IM)
#define IQ 127773
#define IR 2836
#define NTAB 32
#define NDIV (1 + (IM - 1) / NTAB)
#define EPS 1.2e-7
#define RNMX (1.0 - EPS)

class NumberGenerator {
public:
	NumberGenerator(void);
	NumberGenerator(unsigned long inSeed);
	~NumberGenerator(void);

	int operator()(unsigned long inValue) { return getUniformInteger(0, inValue - 1); }

	unsigned int getCurrentSeed(void) { return idum; }
	void setRandomSeed(unsigned long inSeed) { idum = inSeed; }
	bool getUniformBool(void) { return getUniformInteger(0, 1); }
	int getUniformInteger(int inFirst, int inLast);
	double getUniformFloat(double inFirst = 0., double inLast = 1.0);
	double getGaussianFloat(double inMean = 0, double inStdDev = 1);

	double generateRandomNumberInRange(double Min, double Max); // This function generates a random double number between the min and max of the range

private:
	long iy;
	long iv[NTAB];
	long idum;

	int iset;
	double gset;
};

extern NumberGenerator rand1;

#endif