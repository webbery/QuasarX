#include "Algorithms/EMD.h"
#include "ql/math/matrix.hpp"

EMD::EMD() {}

void Interplot3(const Vector<double>& data, int start, int end, Vector<double>& out) {
    // S = a + b(x-x_i) + c(x-x_i)^2 + d(x-x_i)^3
    if (start != 0) {
        out[start] = data[start];
        // out[start] - out[start - 1] = out[start] - out[start + 1];
    }
    if (end != 0) {
        out[end] = data[end];
    }
}

List<Vector<double>> EMD::emd(const Vector<double>& data) {
    List<Vector<double>> imfs;
    // Set k = 0 and find all extrema of r0 = x.
    Set<int> max_indexes, min_indexes;
    for (int i = 1; i < (int)data.size() - 1; ++i) {
        if (data[i] > data[i - 1] && data[i] > data[i+1]) {
            max_indexes.insert(i);
        }
        else if (data[i] < data[i - 1] && data[i] < data[i + 1]) {
            min_indexes.insert(i);
        }
    }
    // Interpolate between minima (maxima) of rk to obtain the lower (upper) envelope emin (emax).

    // Compute the mean envelope m = (emin + emax)/2.

    // Compute the IMF candidate dk+1 = rk − m .
    return imfs;
}
