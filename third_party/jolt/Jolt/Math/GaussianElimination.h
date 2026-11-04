// Lykoware replacement for Jolt's upstream Jolt/Math/GaussianElimination.h (Jolt v5.6.0).
// The upstream file was adapted from Numerical Recipes; this one is written from the
// textbook algorithm instead (ADR 0003, third_party/jolt/PROVENANCE.md).
// Copyright (c) 2026 Joshua Kingsworth. All rights reserved; see the repository LICENSE.
// The rest of third_party/jolt remains under Jolt's MIT licence.

#pragma once

JPH_NAMESPACE_BEGIN

/// Solves A * X = B in place by Gauss-Jordan elimination with partial (row) pivoting: for each
/// column, the row with the largest remaining entry is swapped in as the pivot row, scaled so the
/// pivot is one, and subtracted from every other row. A is N x N and B is N x M; on return B holds
/// X and A has been reduced to the identity (its contents are destroyed either way).
///
/// To invert a matrix, pass it as A and the identity as B; B then holds the inverse.
///
/// @param ioA the N x N coefficient matrix (destroyed)
/// @param ioB the N x M right-hand sides; replaced by the solution
/// @param inTolerance a pivot with a smaller magnitude than this means the matrix is treated as singular
/// @return false if A is singular (B is then left partially modified)
template <class MatrixA, class MatrixB>
bool GaussianElimination(MatrixA &ioA, MatrixB &ioB, float inTolerance = 1.0e-16f)
{
	const uint n = ioA.GetCols();
	const uint m = ioB.GetCols();
	JPH_ASSERT(ioA.GetRows() == n);
	JPH_ASSERT(ioB.GetRows() == n);

	for (uint col = 0; col < n; ++col)
	{
		// Partial pivoting: the remaining row with the largest magnitude in this column
		uint pivot_row = col;
		float pivot_abs = abs(ioA(col, col));
		for (uint row = col + 1; row < n; ++row)
		{
			const float candidate = abs(ioA(row, col));
			if (candidate > pivot_abs)
			{
				pivot_abs = candidate;
				pivot_row = row;
			}
		}
		if (pivot_abs < inTolerance)
			return false;

		if (pivot_row != col)
		{
			for (uint j = 0; j < n; ++j)
				std::swap(ioA(pivot_row, j), ioA(col, j));
			for (uint j = 0; j < m; ++j)
				std::swap(ioB(pivot_row, j), ioB(col, j));
		}

		// Scale the pivot row so the pivot becomes one
		const float inv_pivot = 1.0f / ioA(col, col);
		for (uint j = 0; j < n; ++j)
			ioA(col, j) *= inv_pivot;
		for (uint j = 0; j < m; ++j)
			ioB(col, j) *= inv_pivot;
		ioA(col, col) = 1.0f;

		// Eliminate this column from every other row
		for (uint row = 0; row < n; ++row)
		{
			if (row == col)
				continue;
			const float factor = ioA(row, col);
			if (factor == 0.0f)
				continue;
			for (uint j = 0; j < n; ++j)
				ioA(row, j) -= factor * ioA(col, j);
			for (uint j = 0; j < m; ++j)
				ioB(row, j) -= factor * ioB(col, j);
			ioA(row, col) = 0.0f;
		}
	}

	return true;
}

JPH_NAMESPACE_END
