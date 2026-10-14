#pragma once

#include <vector>

// FFT compleja de base 2 (iterativa), en doble precisión.
class FFT {
public:
    explicit FFT(int n);
    int size() const { return mN; }
    void forward(double *re, double *im) const;

private:
    int mN;
    std::vector<int> mRev;
    std::vector<double> mCos, mSin;
};
