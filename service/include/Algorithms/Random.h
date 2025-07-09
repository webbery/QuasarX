#pragma once
#include "std_header.h"

Vector<double> gauss_noise(double mean, double sigma, int count);
Vector<double> gauss_noise_simd(double mean, double sigma, int count);
