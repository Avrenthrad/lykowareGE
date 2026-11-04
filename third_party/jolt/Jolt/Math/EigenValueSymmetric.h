// Lykoware replacement for Jolt's upstream Jolt/Math/EigenValueSymmetric.h (Jolt v5.6.0).
// The upstream file was adapted from Numerical Recipes; this one is written from the
// textbook algorithm instead (ADR 0003, third_party/jolt/PROVENANCE.md).
// Copyright (c) 2026 Joshua Kingsworth. All rights reserved; see the repository LICENSE.
// The rest of third_party/jolt remains under Jolt's MIT licence.

#pragma once

#include <Jolt/Core/FPException.h>

JPH_NAMESPACE_BEGIN

/// Computes the eigenvalues and eigenvectors of a real symmetric N x N matrix (best for small N)
/// with the cyclic Jacobi method: repeated sweeps that zero each off-diagonal pair with a plane
/// rotation until the off-diagonal part is negligible. See Golub and Van Loan, Matrix Computations,
/// section 8.5 (the 2-by-2 symmetric Schur decomposition and the cyclic Jacobi algorithm).
///
/// Matrix is an N x N matrix type and Vector an N vector type, both indexed with float elements.
/// @param inMatrix is the symmetric matrix of which to return the eigenvalues and vectors
/// @param outEigVec must be the identity on entry; on return its columns are the normalized eigenvectors
/// @param outEigVal on return holds the eigenvalue belonging to each column of outEigVec (unsorted)
/// @return false if the method did not converge
template <class Vector, class Matrix>
bool EigenValueSymmetric(const Matrix &inMatrix, Matrix &outEigVec, Vector &outEigVal)
{
	// Rotations are computed in double precision so that tiny off-diagonal values cannot overflow
	// the intermediate ratio; every step is plain IEEE arithmetic and sqrt, so results are the same
	// on every platform that runs Jolt deterministically
	FPExceptionDisableInvalid disable_invalid;
	JPH_UNUSED(disable_invalid);

	const uint n = inMatrix.GetRows();
	JPH_ASSERT(inMatrix.GetCols() == n);
	JPH_ASSERT(outEigVec.GetRows() == n && outEigVec.GetCols() == n);

	// Work on a copy that converges to a diagonal matrix
	Matrix a = inMatrix;

	// Stop when the off-diagonal part is negligible relative to the whole matrix:
	// off(A)^2 <= eps^2 * |A|_F^2, sums kept in double so large entries cannot overflow
	double frobenius_sq = 0.0;
	for (uint row = 0; row < n; ++row)
		for (uint col = 0; col < n; ++col)
			frobenius_sq += double(a(row, col)) * double(a(row, col));
	const double tolerance_sq = double(FLT_EPSILON) * double(FLT_EPSILON) * frobenius_sq;

	constexpr int cMaxSweeps = 50;
	for (int sweep = 0; sweep < cMaxSweeps; ++sweep)
	{
		double off_diagonal_sq = 0.0;
		for (uint row = 0; row < n; ++row)
			for (uint col = row + 1; col < n; ++col)
				off_diagonal_sq += 2.0 * double(a(row, col)) * double(a(row, col));

		if (off_diagonal_sq <= tolerance_sq)
		{
			for (uint i = 0; i < n; ++i)
				outEigVal[i] = a(i, i);
			return true;
		}

		// One cyclic sweep over every pair (p, q) above the diagonal
		for (uint p = 0; p + 1 < n; ++p)
			for (uint q = p + 1; q < n; ++q)
			{
				const double a_pq = a(p, q);
				if (a_pq == 0.0)
					continue;

				// 2-by-2 symmetric Schur decomposition: cosine c and sine s of the rotation J that
				// makes element (p, q) of J^T A J zero, choosing the smaller angle for stability
				const double tau = (double(a(q, q)) - double(a(p, p))) / (2.0 * a_pq);
				const double root = std::sqrt(1.0 + tau * tau);
				const double t = tau >= 0.0? 1.0 / (tau + root) : -1.0 / (root - tau);
				const double c_d = 1.0 / std::sqrt(1.0 + t * t);
				const float c = float(c_d);
				const float s = float(t * c_d);

				// A := A J (columns p and q)
				for (uint k = 0; k < n; ++k)
				{
					const float a_kp = a(k, p);
					const float a_kq = a(k, q);
					a(k, p) = c * a_kp - s * a_kq;
					a(k, q) = s * a_kp + c * a_kq;
				}

				// A := J^T A (rows p and q)
				for (uint k = 0; k < n; ++k)
				{
					const float a_pk = a(p, k);
					const float a_qk = a(q, k);
					a(p, k) = c * a_pk - s * a_qk;
					a(q, k) = s * a_pk + c * a_qk;
				}

				// The rotation zeroes this pair; remove rounding residue
				a(p, q) = 0.0f;
				a(q, p) = 0.0f;

				// Accumulate the eigenvectors: V := V J
				for (uint k = 0; k < n; ++k)
				{
					const float v_kp = outEigVec(k, p);
					const float v_kq = outEigVec(k, q);
					outEigVec(k, p) = c * v_kp - s * v_kq;
					outEigVec(k, q) = s * v_kp + c * v_kq;
				}
			}
	}

	JPH_ASSERT(false, "EigenValueSymmetric did not converge");
	return false;
}

JPH_NAMESPACE_END
