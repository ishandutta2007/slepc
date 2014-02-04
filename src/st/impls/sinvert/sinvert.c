/*
      Implements the shift-and-invert technique for eigenvalue problems.

   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   SLEPc - Scalable Library for Eigenvalue Problem Computations
   Copyright (c) 2002-2013, Universitat Politecnica de Valencia, Spain

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

#include <slepc-private/stimpl.h>          /*I "slepcst.h" I*/

#undef __FUNCT__
#define __FUNCT__ "STApply_Sinvert"
PetscErrorCode STApply_Sinvert(ST st,Vec x,Vec y)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (st->nmat>1) {
    /* generalized eigenproblem: y = (A - sB)^-1 B x */
    ierr = MatMult(st->T[0],x,st->w);CHKERRQ(ierr);
    ierr = STMatSolve(st,st->w,y);CHKERRQ(ierr);
  } else {
    /* standard eigenproblem: y = (A - sI)^-1 x */
    ierr = STMatSolve(st,x,y);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "STApplyTranspose_Sinvert"
PetscErrorCode STApplyTranspose_Sinvert(ST st,Vec x,Vec y)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (st->nmat>1) {
    /* generalized eigenproblem: y = B^T (A - sB)^-T x */
    ierr = STMatSolveTranspose(st,x,st->w);CHKERRQ(ierr);
    ierr = MatMultTranspose(st->T[0],st->w,y);CHKERRQ(ierr);
  } else {
    /* standard eigenproblem: y = (A - sI)^-T x */
    ierr = STMatSolveTranspose(st,x,y);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "STBackTransform_Sinvert"
PetscErrorCode STBackTransform_Sinvert(ST st,PetscInt n,PetscScalar *eigr,PetscScalar *eigi)
{
  PetscInt    j;
#if !defined(PETSC_USE_COMPLEX)
  PetscScalar t;
#endif

  PetscFunctionBegin;
#if !defined(PETSC_USE_COMPLEX)
  for (j=0;j<n;j++) {
    if (eigi[j] == 0) eigr[j] = 1.0 / eigr[j] + st->sigma;
    else {
      t = eigr[j] * eigr[j] + eigi[j] * eigi[j];
      eigr[j] = eigr[j] / t + st->sigma;
      eigi[j] = - eigi[j] / t;
    }
  }
#else
  for (j=0;j<n;j++) {
    eigr[j] = 1.0 / eigr[j] + st->sigma;
  }
#endif
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "STPostSolve_Sinvert"
PetscErrorCode STPostSolve_Sinvert(ST st)
{
  PetscErrorCode ierr;
  PetscScalar    s;

  PetscFunctionBegin;
  if (st->shift_matrix == ST_MATMODE_INPLACE) {
    if (st->nmat>1) {
      if (st->nmat==3) {
        ierr = MatAXPY(st->A[0],-st->sigma*st->sigma,st->A[2],st->str);CHKERRQ(ierr);
        ierr = MatAXPY(st->A[1],-2.0*st->sigma,st->A[2],st->str);CHKERRQ(ierr);
        s = -st->sigma;
      } else s = st->sigma;
      ierr = MatAXPY(st->A[0],s,st->A[1],st->str);CHKERRQ(ierr);
    } else {
      ierr = MatShift(st->A[0],st->sigma);CHKERRQ(ierr);
    }
    st->Astate[0] = ((PetscObject)st->A[0])->state;
    st->setupcalled = 0;
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "STSetUp_Sinvert"
PetscErrorCode STSetUp_Sinvert(ST st)
{
  PetscErrorCode ierr;
  PetscInt       k,nc,nmat=st->nmat;
  PetscScalar    *coeffs;

  PetscFunctionBegin;
  /* if the user did not set the shift, use the target value */
  if (!st->sigma_set) st->sigma = st->defsigma;
  if (nmat<3) {
    /* T[0] = B */
    if (nmat>1) { ierr = PetscObjectReference((PetscObject)st->A[1]);CHKERRQ(ierr); }
    st->T[0] = st->A[1];
    ierr = STMatGAXPY_Private(st,-st->sigma,0.0,1,1,PETSC_TRUE);CHKERRQ(ierr);
    st->P = st->T[PetscMax(nmat-1,1)];
    ierr = PetscObjectReference((PetscObject)st->P);CHKERRQ(ierr);
  } else {
    if (st->transform) {
      nc = (nmat*(nmat+1))/2;
      ierr = PetscMalloc(nc*sizeof(PetscScalar),&coeffs);CHKERRQ(ierr);
      /* Compute coeffs */
      ierr = STCoeffs_Monomial(st,coeffs);CHKERRQ(ierr);
      /* T[0] = A_n */
      k = nmat-1;
      ierr = PetscObjectReference((PetscObject)st->A[k]);CHKERRQ(ierr);
      st->T[0] = st->A[k];
      for (k=1;k<nmat-1;k++) {
        ierr = STMatMAXPY_Private(st,st->sigma,nmat-k-1,coeffs+(k*(k+1))/2,PETSC_TRUE,&st->T[k],PETSC_FALSE);CHKERRQ(ierr);
      }
      k = nmat-1;
      ierr = STMatMAXPY_Private(st,st->sigma,nmat-k-1,coeffs+(k*(k+1))/2,PETSC_TRUE,&st->T[k],PETSC_TRUE);CHKERRQ(ierr);
      ierr = PetscFree(coeffs);CHKERRQ(ierr);
      st->P = st->T[PetscMax(nmat-1,1)];
      ierr = PetscObjectReference((PetscObject)st->P);CHKERRQ(ierr);
    } else {
      for (k=0;k<nmat;k++) {
        ierr = PetscObjectReference((PetscObject)st->A[k]);CHKERRQ(ierr);
        st->T[k] = st->A[k];
      }
      ierr = PetscMalloc(nmat*sizeof(PetscScalar),&coeffs);CHKERRQ(ierr);
      ierr = STEvaluateCoeffs(st,st->sigma,coeffs);CHKERRQ(ierr);
      ierr = STMatMAXPY_Private(st,1.0,0,coeffs,PETSC_TRUE,&st->P,PETSC_TRUE);CHKERRQ(ierr);
      ierr = PetscFree(coeffs);CHKERRQ(ierr);
      /*ierr = STMatMAXPY_Private(st,st->sigma,0,NULL,PETSC_TRUE,&st->P,PETSC_TRUE);CHKERRQ(ierr);*/
    } 
  }
  if (st->P) {
    if (!st->ksp) { ierr = STGetKSP(st,&st->ksp);CHKERRQ(ierr); }
    ierr = KSPSetOperators(st->ksp,st->P,st->P,DIFFERENT_NONZERO_PATTERN);CHKERRQ(ierr);
    ierr = KSPSetUp(st->ksp);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "STSetShift_Sinvert"
PetscErrorCode STSetShift_Sinvert(ST st,PetscScalar newshift)
{
  PetscErrorCode ierr;
  MatStructure   flg;
  PetscInt       nmat=st->nmat,k,nc;
  PetscScalar    *coeffs;

  PetscFunctionBegin;
  /* Nothing to be done if STSetUp has not been called yet */
  if (!st->setupcalled) PetscFunctionReturn(0);
  if (st->nmat<3) {
    ierr = STMatGAXPY_Private(st,-newshift,-st->sigma,1,1,PETSC_FALSE);CHKERRQ(ierr);
    if (st->P!=st->T[1]) {
      ierr = MatDestroy(&st->P);CHKERRQ(ierr);
      st->P = st->T[1];
      ierr = PetscObjectReference((PetscObject)st->P);CHKERRQ(ierr);
    }    
  } else {
    if (st->transform) {
      if (st->shift_matrix == ST_MATMODE_COPY) {
        nc = (nmat*(nmat+1))/2;
        ierr = PetscMalloc(nc*sizeof(PetscScalar),&coeffs);CHKERRQ(ierr);
        /* Compute coeffs */
        ierr = STCoeffs_Monomial(st,coeffs);CHKERRQ(ierr);
        for (k=1;k<nmat;k++) {
          ierr = STMatMAXPY_Private(st,newshift,nmat-k-1,coeffs+(k*(k+1))/2,PETSC_TRUE,&st->T[k],PETSC_TRUE);CHKERRQ(ierr);
        }
        ierr = PetscFree(coeffs);CHKERRQ(ierr);
      } else {
        for (k=1;k<nmat-1;k++) {
          ierr = STMatMAXPY_Private(st,newshift,nmat-k-1,NULL,PETSC_FALSE,&st->T[k],PETSC_FALSE);CHKERRQ(ierr);
        }
        ierr = STMatMAXPY_Private(st,newshift,0,NULL,PETSC_FALSE,&st->T[nmat-1],PETSC_TRUE);CHKERRQ(ierr);
      }
      if (st->P!=st->T[nmat-1]) {
        ierr = MatDestroy(&st->P);CHKERRQ(ierr);
        st->P = st->T[nmat-1];
        ierr = PetscObjectReference((PetscObject)st->P);CHKERRQ(ierr);
      }
    } else {
      ierr = STMatMAXPY_Private(st,newshift,0,NULL,PETSC_FALSE,&st->P,PETSC_TRUE);CHKERRQ(ierr);
    }
  }
  /* Check if the new KSP matrix has the same zero structure */
  if (st->nmat>1 && st->str == DIFFERENT_NONZERO_PATTERN && (st->sigma == 0.0 || newshift == 0.0)) flg = DIFFERENT_NONZERO_PATTERN;
  else flg = SAME_NONZERO_PATTERN;
  ierr = KSPSetOperators(st->ksp,st->P,st->P,flg);CHKERRQ(ierr);
  ierr = KSPSetUp(st->ksp);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "STSetFromOptions_Sinvert"
PetscErrorCode STSetFromOptions_Sinvert(ST st)
{
  PetscErrorCode ierr;
  PC             pc;
  PCType         pctype;
  KSPType        ksptype;

  PetscFunctionBegin;
  if (!st->ksp) { ierr = STGetKSP(st,&st->ksp);CHKERRQ(ierr); }
  ierr = KSPGetPC(st->ksp,&pc);CHKERRQ(ierr);
  ierr = KSPGetType(st->ksp,&ksptype);CHKERRQ(ierr);
  ierr = PCGetType(pc,&pctype);CHKERRQ(ierr);
  if (!pctype && !ksptype) {
    if (st->shift_matrix == ST_MATMODE_SHELL) {
      /* in shell mode use GMRES with Jacobi as the default */
      ierr = KSPSetType(st->ksp,KSPGMRES);CHKERRQ(ierr);
      ierr = PCSetType(pc,PCJACOBI);CHKERRQ(ierr);
    } else {
      /* use direct solver as default */
      ierr = KSPSetType(st->ksp,KSPPREONLY);CHKERRQ(ierr);
      ierr = PCSetType(pc,PCREDUNDANT);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "STCreate_Sinvert"
PETSC_EXTERN PetscErrorCode STCreate_Sinvert(ST st)
{
  PetscFunctionBegin;
  st->ops->apply           = STApply_Sinvert;
  st->ops->getbilinearform = STGetBilinearForm_Default;
  st->ops->applytrans      = STApplyTranspose_Sinvert;
  st->ops->postsolve       = STPostSolve_Sinvert;
  st->ops->backtransform   = STBackTransform_Sinvert;
  st->ops->setup           = STSetUp_Sinvert;
  st->ops->setshift        = STSetShift_Sinvert;
  st->ops->setfromoptions  = STSetFromOptions_Sinvert;
  st->ops->checknullspace  = STCheckNullSpace_Default;
  PetscFunctionReturn(0);
}
