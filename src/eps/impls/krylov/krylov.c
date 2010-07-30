/*                       
   Common subroutines for all Krylov-type solvers.

   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   SLEPc - Scalable Library for Eigenvalue Problem Computations
   Copyright (c) 2002-2009, Universidad Politecnica de Valencia, Spain

   This file is part of SLEPc.
      
   SLEPc is free software: you can redistribute it and/or modify it under  the
   terms of version 3 of the GNU Lesser General Public License as published by
   the Free Software Foundation.

   SLEPc  is  distributed in the hope that it will be useful, but WITHOUT  ANY 
   WARRANTY;  without even the implied warranty of MERCHANTABILITY or  FITNESS 
   FOR  A  PARTICULAR PURPOSE. See the GNU Lesser General Public  License  for 
   more details.

   You  should have received a copy of the GNU Lesser General  Public  License
   along with SLEPc. If not, see <http://www.gnu.org/licenses/>.
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
*/

#include "private/epsimpl.h"                /*I "slepceps.h" I*/
#include "slepcblaslapack.h"

#undef __FUNCT__  
#define __FUNCT__ "EPSBasicArnoldi"
/*
   EPSBasicArnoldi - Computes an m-step Arnoldi factorization. The first k
   columns are assumed to be locked and therefore they are not modified. On
   exit, the following relation is satisfied:

                    OP * V - V * H = f * e_m^T

   where the columns of V are the Arnoldi vectors (which are B-orthonormal),
   H is an upper Hessenberg matrix, f is the residual vector and e_m is
   the m-th vector of the canonical basis. The vector f is B-orthogonal to
   the columns of V. On exit, beta contains the B-norm of f and the next 
   Arnoldi vector can be computed as v_{m+1} = f / beta. 
*/
PetscErrorCode EPSBasicArnoldi(EPS eps,PetscTruth trans,PetscScalar *H,PetscInt ldh,Vec *V,PetscInt k,PetscInt *M,Vec f,PetscReal *beta,PetscTruth *breakdown)
{
  PetscErrorCode ierr;
  PetscInt       j,m = *M;
  PetscReal      norm;

  PetscFunctionBegin;
  
  for (j=k;j<m-1;j++) {
    if (trans) { ierr = STApplyTranspose(eps->OP,V[j],V[j+1]);CHKERRQ(ierr); }
    else { ierr = STApply(eps->OP,V[j],V[j+1]);CHKERRQ(ierr); }
    ierr = IPOrthogonalize(eps->ip,eps->nds,eps->DS,j+1,PETSC_NULL,V,V[j+1],H+ldh*j,&norm,breakdown);CHKERRQ(ierr);
    H[j+1+ldh*j] = norm;
    if (*breakdown) {
      *M = j+1;
      *beta = norm;
      PetscFunctionReturn(0);
    } else {
      ierr = VecScale(V[j+1],1/norm);CHKERRQ(ierr);
    }
  }
  if (trans) { ierr = STApplyTranspose(eps->OP,V[m-1],f);CHKERRQ(ierr); }
  else { ierr = STApply(eps->OP,V[m-1],f);CHKERRQ(ierr); }
  ierr = IPOrthogonalize(eps->ip,eps->nds,eps->DS,m,PETSC_NULL,V,f,H+ldh*(m-1),beta,PETSC_NULL);CHKERRQ(ierr);
  
  PetscFunctionReturn(0);
}

#undef __FUNCT__  
#define __FUNCT__ "ArnoldiResiduals"
/*
   EPSArnoldiResiduals - Computes the 2-norm of the residual vectors from
   the information provided by the m-step Arnoldi factorization,

                    OP * V - V * H = f * e_m^T

   For the approximate eigenpair (k_i,V*y_i), the residual norm is computed as
   |beta*y(end,i)| where beta is the norm of f and y is the corresponding 
   eigenvector of H.
*/
PetscErrorCode ArnoldiResiduals(PetscScalar *H,PetscInt ldh_,PetscScalar *U,PetscScalar *Y,PetscReal beta,PetscInt nconv,PetscInt ncv_,PetscScalar *eigr,PetscScalar *eigi,PetscReal *errest,PetscScalar *work)
{
#if defined(SLEPC_MISSING_LAPACK_TREVC)
  PetscFunctionBegin;
  SETERRQ(PETSC_ERR_SUP,"TREVC - Lapack routine is unavailable.");
#else
  PetscErrorCode ierr;
  PetscInt       i;
  PetscBLASInt   mout,info,ldh,ncv,inc = 1;
  PetscScalar   tmp;
  PetscReal     norm;
#if defined(PETSC_USE_COMPLEX)
  PetscReal      *rwork=(PetscReal*)(work+3*ncv_);
#else
  PetscReal     normi;
#endif

  PetscFunctionBegin;
  ldh = PetscBLASIntCast(ldh_);
  ncv = PetscBLASIntCast(ncv_);
  if (!Y) Y=work+4*ncv_;

  /* Compute eigenvectors Y of H */
  ierr = PetscMemcpy(Y,U,ncv*ncv*sizeof(PetscScalar));CHKERRQ(ierr);
  ierr = PetscLogEventBegin(EPS_Dense,0,0,0,0);CHKERRQ(ierr);
#if !defined(PETSC_USE_COMPLEX)
  LAPACKtrevc_("R","B",PETSC_NULL,&ncv,H,&ldh,PETSC_NULL,&ncv,Y,&ncv,&ncv,&mout,work,&info);
#else
  LAPACKtrevc_("R","B",PETSC_NULL,&ncv,H,&ldh,PETSC_NULL,&ncv,Y,&ncv,&ncv,&mout,work,rwork,&info);
#endif
  ierr = PetscLogEventEnd(EPS_Dense,0,0,0,0);CHKERRQ(ierr);
  if (info) SETERRQ1(PETSC_ERR_LIB,"Error in Lapack xTREVC %i",info);

  /* normalize eigenvectors */
  for (i=0;i<ncv;i++) {
#if !defined(PETSC_USE_COMPLEX)
    if (eigi[i] != 0.0) {
      norm = BLASnrm2_(&ncv,Y+i*ncv,&inc);
      normi = BLASnrm2_(&ncv,Y+(i+1)*ncv,&inc);
      tmp = 1.0 / SlepcAbsEigenvalue(norm,normi);
      BLASscal_(&ncv,&tmp,Y+i*ncv,&inc);
      BLASscal_(&ncv,&tmp,Y+(i+1)*ncv,&inc);
      i++;     
    } else
#endif
    {
      norm = BLASnrm2_(&ncv,Y+i*ncv,&inc);
      tmp = 1.0 / norm;
      BLASscal_(&ncv,&tmp,Y+i*ncv,&inc);
    }
  }

  /* Compute residual norm estimates as beta*abs(Y(m,:)) */
  for (i=nconv;i<ncv;i++) { 
#if !defined(PETSC_USE_COMPLEX)
    if (eigi[i] != 0 && i<ncv-1) {
      errest[i] = beta*SlepcAbsEigenvalue(Y[i*ncv+ncv-1],Y[(i+1)*ncv+ncv-1]);
      errest[i+1] = errest[i];
      i++;
    } else
#endif
    errest[i] = beta*PetscAbsScalar(Y[i*ncv+ncv-1]);
  }  
  PetscFunctionReturn(0);
#endif
}

#undef __FUNCT__  
#define __FUNCT__ "ArnoldiResiduals2"
/*
   EPSArnoldiResiduals - Estimates the 2-norm of the residual vectors from
   the information provided by the m-step Arnoldi factorization,

                    OP * V - V * H = f * e_m^T

   For the approximate eigenpair (k_i,V*y_i), the residual norm is computed as
   |beta*y(end,i)| where beta is the norm of f and y is the corresponding 
   eigenvector of H.

   Input Parameters:
     H - (quasi-)triangular matrix (dimension nv, leading dimension ldh)
     U - orthogonal transformation matrix (dimension nv, leading dimension nv)
     beta - norm of f
     i - which eigenvector to process
     iscomplex - true if a complex conjugate pair (in real scalars)

   Output parameters:
     Y - computed eigenvectors, 2 columns if iscomplex=true (leading dimension nv)
     est - computed residual norm estimate

   Workspace:
     work is workspace to store 3*nv scalars, nv booleans and nv reals
*/
PetscErrorCode ArnoldiResiduals2(PetscScalar *H,PetscInt ldh_,PetscScalar *U,PetscScalar *Y,PetscReal beta,PetscInt i,PetscTruth iscomplex,PetscInt nv_,PetscReal *est,PetscScalar *work)
{
#if defined(SLEPC_MISSING_LAPACK_TREVC)
  PetscFunctionBegin;
  SETERRQ(PETSC_ERR_SUP,"TREVC - Lapack routine is unavailable.");
#else
  PetscErrorCode ierr;
  PetscInt       k;
  PetscBLASInt   mm,mout,info,ldh,nv,inc = 1;
  PetscScalar    tmp,done=1.0,zero=0.0;
  PetscReal      norm;
  PetscTruth     *select=(PetscTruth*)(work+4*nv_);
#if defined(PETSC_USE_COMPLEX)
  PetscReal      *rwork=(PetscReal*)(work+3*nv_);
#endif

  PetscFunctionBegin;
  ldh = PetscBLASIntCast(ldh_);
  nv = PetscBLASIntCast(nv_);
  for (k=0;k<nv;k++) select[k] = PETSC_FALSE;

  /* Compute eigenvectors Y of H */
  mm = iscomplex? 2: 1;
  select[i] = PETSC_TRUE;
#if !defined(PETSC_USE_COMPLEX)
  if (iscomplex) select[i+1] = PETSC_TRUE;
  LAPACKtrevc_("R","S",select,&nv,H,&ldh,PETSC_NULL,&nv,Y,&nv,&mm,&mout,work,&info);
#else
  LAPACKtrevc_("R","S",select,&nv,H,&ldh,PETSC_NULL,&nv,Y,&nv,&mm,&mout,work,rwork,&info);
#endif
  if (info) SETERRQ1(PETSC_ERR_LIB,"Error in Lapack xTREVC %i",info);
  if (mout != mm) SETERRQ(PETSC_ERR_ARG_WRONG,"Inconsistent arguments");
  ierr = PetscMemcpy(work,Y,mout*nv*sizeof(PetscScalar));CHKERRQ(ierr);

  /* accumulate and normalize eigenvectors */
  BLASgemv_("N",&nv,&nv,&done,U,&nv,work,&inc,&zero,Y,&inc);
#if !defined(PETSC_USE_COMPLEX)
  if (iscomplex) BLASgemv_("N",&nv,&nv,&done,U,&nv,work+nv,&inc,&zero,Y+nv,&inc);
#endif
  mm = mm*nv;
  norm = BLASnrm2_(&mm,Y,&inc);
  tmp = 1.0 / norm;
  BLASscal_(&mm,&tmp,Y,&inc);

  /* Compute residual norm estimate as beta*abs(Y(m,:)) */
#if !defined(PETSC_USE_COMPLEX)
  if (iscomplex) {
    *est = beta*SlepcAbsEigenvalue(Y[nv-1],Y[2*nv-1]);
  } else
#endif
  *est = beta*PetscAbsScalar(Y[nv-1]);
    
  PetscFunctionReturn(0);
#endif
}

#undef __FUNCT__  
#define __FUNCT__ "EPSKrylovConvergence"
/*
   EPSKrylovConvergence - Implements the loop that checks for convergence
   in Krylov methods.

   Input Parameters:
     eps   - the eigensolver; some error estimates are updated in eps->errest 
     issym - whether the projected problem is symmetric or not
     kini  - initial value of k (the loop variable)
     nits  - number of iterations of the loop
     S     - Schur form of projected matrix (not referenced if issym)
     lds   - leading dimension of S
     Q     - Schur vectors of projected matrix (eigenvectors if issym)
     V     - set of basis vectors (used only if trueresidual is activated)
     nv    - number of vectors to process (dimension of Q, columns of V)
     beta  - norm of f (the residual vector of the Arnoldi/Lanczos factorization)
     corrf - correction factor for residual estimates (only in harmonic KS)

   Output Parameters:
     kout  - the first index where the convergence test failed

   Workspace:
     work is workspace to store 5*nv scalars, nv booleans and nv reals (only if !issym)
*/
PetscErrorCode EPSKrylovConvergence(EPS eps,PetscTruth issym,PetscInt kini,PetscInt nits,PetscScalar *S,PetscInt lds,PetscScalar *Q,Vec *V,PetscInt nv,PetscReal beta,PetscReal corrf,PetscInt *kout,PetscScalar *work)
{
  PetscErrorCode ierr;
  PetscInt       k,marker;
  PetscScalar    re,im,*Z,*work2;
  PetscReal      resnorm;
  PetscTruth     iscomplex,conv,isshift;

  PetscFunctionBegin;
  if (!issym) { Z = work; work2 = work+2*nv; }
  ierr = PetscTypeCompare((PetscObject)eps->OP,STSHIFT,&isshift);CHKERRQ(ierr);
  marker = -1;
  for (k=kini;k<kini+nits;k++) {
    /* eigenvalue */
    re = eps->eigr[k];
    im = eps->eigi[k];
    if (eps->trueres || isshift) {
      ierr = STBackTransform(eps->OP,1,&re,&im);CHKERRQ(ierr);
    }
    iscomplex = PETSC_FALSE;
    if (!issym && k<nv-1 && S[k+1+k*lds] != 0.0) iscomplex = PETSC_TRUE;
    /* residual norm */
    if (issym) {
      resnorm = beta*PetscAbsScalar(Q[(k-kini+1)*nv-1]);
    } else {
      ierr = ArnoldiResiduals2(S,lds,Q,Z,beta,k,iscomplex,nv,&resnorm,work2);CHKERRQ(ierr);
    }
    if (eps->trueres) {
      if (issym) Z = Q+(k-kini)*nv;
      ierr = EPSComputeTrueResidual(eps,re,im,Z,V,nv,&resnorm);CHKERRQ(ierr);
    }
    else resnorm *= corrf;
    /* error estimate */
    eps->errest[k] = resnorm;
    ierr = (*eps->conv_func)(eps,re,im,&eps->errest[k],&conv,eps->conv_ctx);CHKERRQ(ierr);
    if (marker==-1 && !conv) marker = k;
    if (iscomplex) { eps->errest[k+1] = eps->errest[k]; k++; }
    if (marker!=-1 && !eps->trackall) break;
  }
  if (marker!=-1) k = marker;
  *kout = k;

  PetscFunctionReturn(0);
}

#undef __FUNCT__  
#define __FUNCT__ "EPSFullLanczos"
/*
   EPSFullLanczos - Computes an m-step Lanczos factorization with full
   reorthogonalization.  At each Lanczos step, the corresponding Lanczos
   vector is orthogonalized with respect to all previous Lanczos vectors.
   This is equivalent to computing an m-step Arnoldi factorization and
   exploting symmetry of the operator.

   The first k columns are assumed to be locked and therefore they are 
   not modified. On exit, the following relation is satisfied:

                    OP * V - V * T = f * e_m^T

   where the columns of V are the Lanczos vectors (which are B-orthonormal),
   T is a real symmetric tridiagonal matrix, f is the residual vector and e_m
   is the m-th vector of the canonical basis. The tridiagonal is stored as
   two arrays: alpha contains the diagonal elements, beta the off-diagonal.
   The vector f is B-orthogonal to the columns of V. On exit, the last element
   of beta contains the B-norm of f and the next Lanczos vector can be 
   computed as v_{m+1} = f / beta(end). 

*/
PetscErrorCode EPSFullLanczos(EPS eps,PetscReal *alpha,PetscReal *beta,Vec *V,PetscInt k,PetscInt *M,Vec f,PetscTruth *breakdown)
{
  PetscErrorCode ierr;
  PetscInt       j,m = *M;
  PetscReal      norm;
  PetscScalar    *hwork,lhwork[100];

  PetscFunctionBegin;
  if (m > 100) {
    ierr = PetscMalloc((eps->nds+m)*sizeof(PetscScalar),&hwork);CHKERRQ(ierr);
  } else {
    hwork = lhwork;
  }

  for (j=k;j<m-1;j++) {
    ierr = STApply(eps->OP,V[j],V[j+1]);CHKERRQ(ierr);
    ierr = IPOrthogonalize(eps->ip,eps->nds,eps->DS,j+1,PETSC_NULL,V,V[j+1],hwork,&norm,breakdown);CHKERRQ(ierr);
    alpha[j-k] = PetscRealPart(hwork[j]);
    beta[j-k] = norm;
    if (*breakdown) {
      *M = j+1;
      if (m > 100) {
        ierr = PetscFree(hwork);CHKERRQ(ierr);
      }
      PetscFunctionReturn(0);
    } else {
      ierr = VecScale(V[j+1],1.0/norm);CHKERRQ(ierr);
    }
  }
  ierr = STApply(eps->OP,V[m-1],f);CHKERRQ(ierr);
  ierr = IPOrthogonalize(eps->ip,eps->nds,eps->DS,m,PETSC_NULL,V,f,hwork,&norm,PETSC_NULL);CHKERRQ(ierr);
  alpha[m-1-k] = PetscRealPart(hwork[m-1]); 
  beta[m-1-k] = norm;
  
  if (m > 100) {
    ierr = PetscFree(hwork);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}


