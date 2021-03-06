//
// This file is part of the libWetHair open source project
//
// The code is licensed solely for academic and non-commercial use under the
// terms of the Clear BSD License. The terms of the Clear BSD License are
// provided below. Other licenses may be obtained by contacting the faculty
// of the Columbia Computer Graphics Group or a Columbia University licensing officer.
//
// The Clear BSD License
//
// Copyright 2015 Xinxin Zhang
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted (subject to the limitations in the disclaimer
// below) provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//  list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//  this list of conditions and the following disclaimer in the documentation
//  and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its contributors may be used
//  to endorse or promote products derived from this software without specific
//  prior written permission.
//
// NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS
// LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
// GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
// OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.

#ifndef _AMG_H_
#define _AMG_H_

#include <iostream>
#include <vector>
#include <tbb/tbb.h>
#include <thread>
#include <cmath>
#include "MathDefs.h"
#include "pcgsolver/sparse_matrix.h"
#include "pcgsolver/blas_wrapper.h"
#include "GeometricLevelGen.h"
/*
given A_L, R_L, P_L, b,compute x using
Multigrid Cycles.

*/
//#define AMG_VERBOSE

using namespace std;
using namespace BLAS;
template<class T>
void RBGS(const FixedSparseMatrix<T> &A, 
	const vector<T> &b,
	vector<T> &x, 
	int ni, int nj, int nk, int iternum)
{

	for (int iter=0;iter<iternum;iter++)
	{
		size_t num = ni*nj*nk;
		size_t slice = ni*nj;
		tbb::parallel_for((size_t)0, num, (size_t)1, [&](size_t thread_idx){
			int k = thread_idx/slice;
			int j = (thread_idx%slice)/ni;
			int i = thread_idx%ni;
			if(k<nk && j<nj && i<ni)
			{
				if ((i+j+k)%2 == 1)
				{
					unsigned int index = (unsigned int)thread_idx;
					T sum = 0;
					T diag= 0;
					for (int ii=A.rowstart[index];ii<A.rowstart[index+1];ii++)
					{
						if(A.colindex[ii]!=index)//none diagonal terms
						{
							sum += A.value[ii]*x[A.colindex[ii]];
						}
						else//record diagonal value A(i,i)
						{
							diag = A.value[ii];
						}
					}//A(i,:)*x for off-diag terms
					if(diag!=0)
					{
						x[index] = (b[index] - sum)/diag;
					}
					else
					{
						x[index] = 0;
					}
				}
			}

		});

		tbb::parallel_for((size_t)0, num, (size_t)1, [&](size_t thread_idx){
			int k = thread_idx/slice;
			int j = (thread_idx%slice)/ni;
			int i = thread_idx%ni;
			if(k<nk && j<nj && i<ni)
			{
				if ((i+j+k)%2 == 0)
				{
					unsigned int index = (unsigned int)thread_idx;
					T sum = 0;
					T diag= 0;
					for (int ii=A.rowstart[index];ii<A.rowstart[index+1];ii++)
					{
						if(A.colindex[ii]!=index)//none diagonal terms
						{
							sum += A.value[ii]*x[A.colindex[ii]];
						}
						else//record diagonal value A(i,i)
						{
							diag = A.value[ii];
						}
					}//A(i,:)*x for off-diag terms
					if(diag!=0)
					{
						x[index] = (b[index] - sum)/diag;
					}
					else
					{
						x[index] = 0;
					}
				}
			}

		});
	}
}

template<class T>
void restriction(const FixedSparseMatrix<T> &R,
	const FixedSparseMatrix<T> &A,
	const vector<T>            &x,
	const vector<T>            &b_curr,
	vector<T>                  &b_next)
{
	b_next.assign(b_next.size(),0);
	vector<T> r = b_curr;
	multiply_and_subtract(A,x,r);
	multiply(R,r,b_next);
	r.resize(0);
}
template<class T>
void prolongatoin(const FixedSparseMatrix<T> &P,
	const vector<T>            &x_curr,
	vector<T>                  &x_next)
{
	static vector<T> xx;
	xx.resize(x_next.size());
	xx.assign(xx.size(),0);
	multiply(P,x_curr,xx);//xx = P*x_curr;
	add_scaled(1.0,xx,x_next);
	xx.resize(0);
}
template<class T>
void amgVCycle(vector<FixedSparseMatrix<T> > &A_L,
	vector<FixedSparseMatrix<T> > &R_L,
	vector<FixedSparseMatrix<T> > &P_L,
	vector<Vector3i>                  &S_L,
	vector<T>                      &x,
	const vector<T>                &b)
{
	int total_level = A_L.size();
	vector<vector<T>> x_L;
	vector<vector<T>> b_L;
	x_L.resize(total_level);
	b_L.resize(total_level);
	b_L[0] = b;
	x_L[0] = x;
	for(int i=1;i<total_level;i++)
	{
		int unknowns = S_L[i][0]*S_L[i][1]*S_L[i][2];
		x_L[i].resize(unknowns);
		x_L[i].assign(x_L[i].size(),0);
		b_L[i].resize(unknowns);
		b_L[i].assign(b_L[i].size(),0);
	}

	for (int i=0;i<total_level-1;i++)
	{
		RBGS((A_L[i]),b_L[i],x_L[i],S_L[i][0],S_L[i][1],S_L[i][2],4);
		restriction((R_L[i]),(A_L[i]),x_L[i],b_L[i],b_L[i+1]);
	}
	int i = total_level-1;
	RBGS((A_L[i]),b_L[i],x_L[i],S_L[i][0],S_L[i][1],S_L[i][2],200);
	for (int i=total_level-2;i>=0;i--)
	{
		prolongatoin(*(P_L[i]),x_L[i+1],x_L[i]);
		RBGS((A_L[i]),b_L[i],x_L[i],S_L[i][0],S_L[i][1],S_L[i][2],4);
	}
	x = x_L[0];

	for(int i=0;i<total_level;i++)
	{

		x_L[i].resize(0);
		x_L[i].shrink_to_fit();
		b_L[i].resize(0);
		b_L[i].shrink_to_fit();
	}
}
template<class T>
void amgPrecond(vector<FixedSparseMatrix<T> > &A_L,
	vector<FixedSparseMatrix<T> > &R_L,
	vector<FixedSparseMatrix<T> > &P_L,
	vector<Vector3i>                  &S_L,
	vector<T>                      &x,
	const vector<T>                &b)
{
	x.resize(b.size());
	x.assign(x.size(),0);
	amgVCycle(A_L,R_L,P_L,S_L,x,b);
}
template<class T>
bool AMGPCGSolve(const SparseMatrix<T> &matrix, 
	const std::vector<T> &rhs, 
	std::vector<T> &result, 
	T tolerance_factor,
	int max_iterations,
	T &residual_out, 
	int &iterations_out,
	int ni, int nj, int nk) 
{
	FixedSparseMatrix<T> fixed_matrix;
	fixed_matrix.construct_from_matrix(matrix);
	vector<FixedSparseMatrix<T> > A_L;
	vector<FixedSparseMatrix<T> > R_L;
	vector<FixedSparseMatrix<T> > P_L;
	vector<Vector3i>                  S_L;
	vector<T>                      m,z,s,r;
	int total_level;
	levelGen<T> amg_levelGen;
	amg_levelGen.generateLevelsGalerkinCoarsening(A_L,R_L,P_L,S_L,total_level,fixed_matrix,ni,nj,nk);

	unsigned int n=matrix.n;
	if(m.size()!=n){ m.resize(n); s.resize(n); z.resize(n); r.resize(n); }
	zero(result);
	r=rhs;
	residual_out=BLAS::abs_max(r);
	if(residual_out==0) {
		iterations_out=0;


		for (int i=0; i<total_level; i++)
		{
			A_L[i].clear();
		}
		for (int i=0; i<total_level-1; i++)
		{

			R_L[i].clear();
			P_L[i].clear();

		}

		return true;
	}
	double tol=tolerance_factor*residual_out;

	amgPrecond(A_L,R_L,P_L,S_L,z,r);
	double rho=BLAS::dot(z, r);
	if(rho==0 || rho!=rho) {
		for (int i=0; i<total_level; i++)
		{
			A_L[i].clear();

		}
		for (int i=0; i<total_level-1; i++)
		{

			R_L[i].clear();
			P_L[i].clear();

		}
		iterations_out=0;
		return false;
	}

	s=z;

	int iteration;
	for(iteration=0; iteration<max_iterations; ++iteration){
		multiply(fixed_matrix, s, z);
		double alpha=rho/BLAS::dot(s, z);
		BLAS::add_scaled(alpha, s, result);
		BLAS::add_scaled(-alpha, z, r);
		residual_out=BLAS::abs_max(r);
		if(residual_out<=tol) {
			iterations_out=iteration+1;

			for (int i=0; i<total_level; i++)
			{
				A_L[i].clear();

			}
			for (int i=0; i<total_level-1; i++)
			{

				R_L[i].clear();
				P_L[i].clear();

			}

			return true; 
		}
		amgPrecond(A_L,R_L,P_L,S_L,z,r);
		double rho_new=BLAS::dot(z, r);
		double beta=rho_new/rho;
		BLAS::add_scaled(beta, s, z); s.swap(z); // s=beta*s+z
		rho=rho_new;
	}
	iterations_out=iteration;
	for (int i=0; i<total_level; i++)
	{
		A_L[i].clear();

	}
	for (int i=0; i<total_level-1; i++)
	{

		R_L[i].clear();
		P_L[i].clear();

	}
	return false;
}

template<class T>
void RBGS_with_pattern(const FixedSparseMatrix<T> &A,
                       const vector<T> &b,
                       vector<T> &x,
                       vector<bool> & pattern,
                       int iternum)
{
  
  for (int iter=0;iter<iternum;iter++)
  {
    size_t num = x.size();
    
    tbb::parallel_for((size_t)0, num, (size_t)1, [&](size_t thread_idx){
      if(thread_idx >= A.rowstart.size()-1 || thread_idx >= pattern.size()) return;
      if(pattern[thread_idx]==true)
      {
        T sum = 0;
        T diag= 0;
        for (int ii=A.rowstart[thread_idx];ii<A.rowstart[thread_idx+1];ii++)
        {
          if(ii >= A.colindex.size() || ii >= A.value.size()) continue;
          
          if(A.colindex[ii]!=thread_idx && A.colindex[ii] < num)//none diagonal terms
          {
            sum += A.value[ii]*x[A.colindex[ii]];
          }
          else//record diagonal value A(i,i)
          {
            diag = A.value[ii];
          }
        }//A(i,:)*x for off-diag terms
        if(diag!=0)
        {
          x[thread_idx] = (b[thread_idx] - sum)/diag;
        }
        else
        {
          x[thread_idx] = 0;
        }
      }
      
    });
    
    tbb::parallel_for((size_t)0, num, (size_t)1, [&](size_t thread_idx){
      if(thread_idx >= A.rowstart.size()-1 || thread_idx >= pattern.size()) return;
      if(pattern[thread_idx]==false)
      {
        T sum = 0;
        T diag= 0;
        for (int ii=A.rowstart[thread_idx];ii<A.rowstart[thread_idx+1];ii++)
        {
          if(ii >= A.colindex.size() || ii >= A.value.size()) continue;
          
          if(A.colindex[ii]!=thread_idx && A.colindex[ii] < num)//none diagonal terms
          {
            sum += A.value[ii]*x[A.colindex[ii]];
          }
          else//record diagonal value A(i,i)
          {
            diag = A.value[ii];
          }
        }//A(i,:)*x for off-diag terms
        if(diag!=0)
        {
          x[thread_idx] = (b[thread_idx] - sum)/diag;
        }
        else
        {
          x[thread_idx] = 0;
        }
      }
      
    });
  }
}

template<class T>
void amgVCycleCompressed(vector<FixedSparseMatrix<T> > &A_L,
	vector<FixedSparseMatrix<T> > &R_L,
	vector<FixedSparseMatrix<T> > &P_L,
	vector<vector<bool> >         &p_L,
	vector<T>                      &x,
	const vector<T>                &b)
{
	int total_level = A_L.size();
	vector<vector<T>> x_L;
	vector<vector<T>> b_L;
	x_L.resize(total_level);
	b_L.resize(total_level);
	b_L[0] = b;
	x_L[0] = x;
	for(int i=1;i<total_level;i++)
	{
		int unknowns = A_L[i].n;
		x_L[i].resize(unknowns);
		x_L[i].assign(x_L[i].size(),0);
		b_L[i].resize(unknowns);
		b_L[i].assign(b_L[i].size(),0);
	}

	for (int i=0;i<total_level-1;i++)
	{
#ifdef AMG_VERBOSE
		printf("level: %d, RBGS\n", i);
#endif
		RBGS_with_pattern((A_L[i]),b_L[i],x_L[i],(p_L[i]),4);
#ifdef AMG_VERBOSE
		printf("level: %d, restriction\n", i);
#endif
		restriction((R_L[i]),(A_L[i]),x_L[i],b_L[i],b_L[i+1]);
	}
	int i = total_level-1;
#ifdef AMG_VERBOSE
	printf("level: %d, top solve\n", i);
#endif
	RBGS_with_pattern((A_L[i]),b_L[i],x_L[i],(p_L[i]),200);
	for (int i=total_level-2;i>=0;i--)
	{
#ifdef AMG_VERBOSE
		printf("level: %d, prolongation\n", i);
#endif
		prolongatoin((P_L[i]),x_L[i+1],x_L[i]);
#ifdef AMG_VERBOSE
		printf("level: %d, RBGS\n", i);
#endif
		RBGS_with_pattern((A_L[i]),b_L[i],x_L[i],(p_L[i]),4);
	}
	x = x_L[0];

	for(int i=0;i<total_level;i++)
	{

		x_L[i].resize(0);
		x_L[i].shrink_to_fit();
		b_L[i].resize(0);
		b_L[i].shrink_to_fit();
	}
}
template<class T>
void amgPrecondCompressed(vector<FixedSparseMatrix<T> > &A_L,
	vector<FixedSparseMatrix<T> > &R_L,
	vector<FixedSparseMatrix<T> > &P_L,
	vector<vector<bool>  >        &p_L,
	vector<T>                      &x,
	const vector<T>                &b)
{
	//printf("preconditioning begin\n");
	x.resize(b.size());
	x.assign(x.size(),0);
	amgVCycleCompressed(A_L,R_L,P_L,p_L,x,b);
	//printf("preconditioning finished\n");
}
template<class T>
bool AMGPCGSolveCompressed(const SparseMatrix<T> &matrix, 
	const std::vector<T> &rhs, 
	std::vector<T> &result, 
	vector<char> &mask,
	vector<int>  &index_table,
	T tolerance_factor,
	int max_iterations,
	T &residual_out, 
	int &iterations_out,
	int ni, int nj, int nk) 
{
	static FixedSparseMatrix<T> fixed_matrix;
	fixed_matrix.construct_from_matrix(matrix);
	static vector<FixedSparseMatrix<T> > A_L;
	static vector<FixedSparseMatrix<T> > R_L;
	static vector<FixedSparseMatrix<T> > P_L;
	static vector<vector<bool> >          p_L;
	vector<T>                      m,z,s,r;
	int total_level;
	levelGen<T> amg_levelGen;
#ifdef AMG_VERBOSE
	std::cout << "[AMG: generate levels]" << std::endl;
#endif
	amg_levelGen.generateLevelsGalerkinCoarseningCompressed(A_L,R_L,P_L,p_L,
		total_level,fixed_matrix,mask,index_table,ni,nj,nk);

	unsigned int n=matrix.n;
	if(m.size()!=n){ m.resize(n); s.resize(n); z.resize(n); r.resize(n); }
	zero(result);
	r=rhs;
	residual_out=BLAS::abs_max(r);
	if(residual_out==0) {
		iterations_out=0;


		for (int i=0; i<total_level; i++)
		{
			A_L[i].clear();
		}
		for (int i=0; i<total_level-1; i++)
		{

			R_L[i].clear();
			P_L[i].clear();

		}

		return true;
	}
	double tol=tolerance_factor*residual_out;
#ifdef AMG_VERBOSE
	std::cout << "[AMG: preconditioning]" << std::endl;
#endif
	amgPrecondCompressed(A_L,R_L,P_L,p_L,z,r);
#ifdef AMG_VERBOSE
  std::cout<<"[AMG: first precond done]"<< std::endl;
#endif
	double rho=BLAS::dot(z, r);
	if(rho==0 || rho!=rho) {
		for (int i=0; i<total_level; i++)
		{
			A_L[i].clear();

		}
		for (int i=0; i<total_level-1; i++)
		{

			R_L[i].clear();
			P_L[i].clear();

		}
		iterations_out=0;
		return false;
	}

	s=z;
#ifdef AMG_VERBOSE
	std::cout << "[AMG: iterative solve]" << std::endl;
#endif
	int iteration;
	for(iteration=0; iteration<max_iterations; ++iteration){
		multiply(fixed_matrix, s, z);
		//printf("multiply done\n");
		double alpha=rho/BLAS::dot(s, z);
		//printf("%d,%d,%d,%d\n",s.size(),z.size(),r.size(),result.size());
		BLAS::add_scaled(alpha, s, result);
		BLAS::add_scaled(-alpha, z, r);
		residual_out=BLAS::abs_max(r);

		if(residual_out<=tol) {
			iterations_out=iteration+1;

			for (int i=0; i<total_level; i++)
			{
				A_L[i].clear();

			}
			for (int i=0; i<total_level-1; i++)
			{

				R_L[i].clear();
				P_L[i].clear();

			}

			return true; 
		}
#ifdef AMG_VERBOSE
		std::cout << "[AMG: iterative preconditioning]" << std::endl;
#endif
		amgPrecondCompressed(A_L,R_L,P_L,p_L,z,r);
#ifdef AMG_VERBOSE
    std::cout << "[AMG: second precond done]"<< std::endl;
#endif
		double rho_new=BLAS::dot(z, r);
		double beta=rho_new/rho;
		BLAS::add_scaled(beta, s, z); s.swap(z); // s=beta*s+z
		rho=rho_new;
	}
	iterations_out=iteration;
	for (int i=0; i<total_level; i++)
	{
		A_L[i].clear();

	}
	for (int i=0; i<total_level-1; i++)
	{

		R_L[i].clear();
		P_L[i].clear();

	}
	return false;
}

template<class T>
bool AMGPCGSolveSparse(const SparseMatrix<T> &matrix, 
	const std::vector<T> &rhs, 
	std::vector<T> &result, 
	vector<Vector3i> &Dof_ijk,
	T tolerance_factor,
	int max_iterations,
	T &residual_out, 
	int &iterations_out,
	int ni, int nj, int nk) 
{
	static FixedSparseMatrix<T> fixed_matrix;
	fixed_matrix.construct_from_matrix(matrix);
	static vector<FixedSparseMatrix<T> > A_L;
	static vector<FixedSparseMatrix<T> > R_L;
	static vector<FixedSparseMatrix<T> > P_L;
	static vector<vector<bool> >          p_L;
	vector<T>                      m,z,s,r;
	int total_level;
	levelGen<T> amg_levelGen;
#ifdef AMG_VERBOSE
	std::cout << "[AMG: generate levels]" << std::endl;
#endif
	amg_levelGen.generateLevelsGalerkinCoarseningSparse
		(A_L,R_L,P_L,p_L,total_level,fixed_matrix,Dof_ijk,ni,nj,nk);

	unsigned int n=matrix.n;
	if(m.size()!=n){ m.resize(n); s.resize(n); z.resize(n); r.resize(n); }
	zero(result);
	r=rhs;
	residual_out=BLAS::abs_max(r);
	if(residual_out==0) {
		iterations_out=0;


		for (int i=0; i<total_level; i++)
		{
			A_L[i].clear();
		}
		for (int i=0; i<total_level-1; i++)
		{

			R_L[i].clear();
			P_L[i].clear();

		}

		return true;
	}
	double tol=tolerance_factor*residual_out;
#ifdef AMG_VERBOSE
	std::cout << "[AMG: preconditioning]" << std::endl;
#endif
	amgPrecondCompressed(A_L,R_L,P_L,p_L,z,r);
#ifdef AMG_VERBOSE
  std::cout << "[AMG: first precond done]"<< std::endl;
#endif
	double rho=BLAS::dot(z, r);
	if(rho==0 || rho!=rho) {
		for (int i=0; i<total_level; i++)
		{
			A_L[i].clear();

		}
		for (int i=0; i<total_level-1; i++)
		{

			R_L[i].clear();
			P_L[i].clear();

		}
		iterations_out=0;
		return false;
	}

	s=z;
#ifdef AMG_VERBOSE
	std::cout << "[AMG: iterative solve]" << std::endl;
#endif
	int iteration;
	for(iteration=0; iteration<max_iterations; ++iteration){
		multiply(fixed_matrix, s, z);
		//printf("multiply done\n");
		double alpha=rho/BLAS::dot(s, z);
		//printf("%d,%d,%d,%d\n",s.size(),z.size(),r.size(),result.size());
		BLAS::add_scaled(alpha, s, result);
		BLAS::add_scaled(-alpha, z, r);
		residual_out=BLAS::abs_max(r);

		if(residual_out<=tol) {
			iterations_out=iteration+1;

			for (int i=0; i<total_level; i++)
			{
				A_L[i].clear();

			}
			for (int i=0; i<total_level-1; i++)
			{

				R_L[i].clear();
				P_L[i].clear();

			}

			return true; 
		}
#ifdef AMG_VERBOSE
		std::cout << "[AMG: iterative preconditioning]" << std::endl;
#endif
		amgPrecondCompressed(A_L,R_L,P_L,p_L,z,r);
#ifdef AMG_VERBOSE
    std::cout << "[AMG: second precond done]"<< std::endl;
#endif
		double rho_new=BLAS::dot(z, r);
		double beta=rho_new/rho;
		BLAS::add_scaled(beta, s, z); s.swap(z); // s=beta*s+z
		rho=rho_new;
	}
	iterations_out=iteration;
	for (int i=0; i<total_level; i++)
	{
		A_L[i].clear();

	}
	for (int i=0; i<total_level-1; i++)
	{

		R_L[i].clear();
		P_L[i].clear();

	}
	return false;
}

#endif
