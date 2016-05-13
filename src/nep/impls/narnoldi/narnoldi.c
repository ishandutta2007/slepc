/*

   SLEPc nonlinear eigensolver: "narnoldi"

   Method: Nonlinear Arnoldi

   Algorithm:

       Arnoldi for nonlinear eigenproblems.

   References:

       [1] H. Voss, "An Arnoldi method for nonlinear eigenvalue problems",
           BIT 44:387-401, 2004.

   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   SLEPc - Scalable Library for Eigenvalue Problem Computations
   Copyright (c) 2002-2015, Universitat Politecnica de Valencia, Spain

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

#include <slepc/private/nepimpl.h>

#undef __FUNCT__
#define __FUNCT__ "NEPSetUp_NArnoldi"
PetscErrorCode NEPSetUp_NArnoldi(NEP nep)
{
  PetscErrorCode ierr;
  PetscBool      istrivial;

  PetscFunctionBegin;
  ierr = NEPSetDimensions_Default(nep,nep->nev,&nep->ncv,&nep->mpd);CHKERRQ(ierr);
  if (nep->ncv>nep->nev+nep->mpd) SETERRQ(PetscObjectComm((PetscObject)nep),1,"The value of ncv must not be larger than nev+mpd");
  if (nep->nev>1) SETERRQ(PetscObjectComm((PetscObject)nep),PETSC_ERR_SUP,"Requested several eigenpairs but this solver can compute only one");
  if (!nep->max_it) nep->max_it = nep->ncv;
  if (nep->max_it < nep->ncv) SETERRQ(PetscObjectComm((PetscObject)nep),1,"Current implementation is unrestarted, must set max_it >= ncv");
  if (nep->which && nep->which!=NEP_TARGET_MAGNITUDE) SETERRQ(PetscObjectComm((PetscObject)nep),1,"Wrong value of which");
  if (nep->fui!=NEP_USER_INTERFACE_SPLIT) SETERRQ(PetscObjectComm((PetscObject)nep),PETSC_ERR_SUP,"NARNOLDI only available for split operator");

  ierr = RGIsTrivial(nep->rg,&istrivial);CHKERRQ(ierr);
  if (!istrivial) SETERRQ(PetscObjectComm((PetscObject)nep),PETSC_ERR_SUP,"This solver does not support region filtering");

  ierr = NEPAllocateSolution(nep,0);CHKERRQ(ierr);
  ierr = NEPSetWorkVecs(nep,3);CHKERRQ(ierr);

  /* set-up DS and transfer split operator functions */
  ierr = DSSetType(nep->ds,DSNEP);CHKERRQ(ierr);
  ierr = DSNEPSetFN(nep->ds,nep->nt,nep->f);CHKERRQ(ierr);
  ierr = DSAllocate(nep->ds,nep->ncv);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "NEPSolve_NArnoldi"
PetscErrorCode NEPSolve_NArnoldi(NEP nep)
{
  PetscErrorCode     ierr;
  Mat                T=nep->function,Tsigma;
  Vec                f,r=nep->work[0],x=nep->work[1],w=nep->work[2];
  PetscScalar        *X,lambda;
  PetscReal          beta,resnorm=0.0,nrm;
  PetscInt           n;
  PetscBool          breakdown;
  KSPConvergedReason kspreason;

  PetscFunctionBegin;
  /* get initial space and shift */
  ierr = NEPGetDefaultShift(nep,&lambda);CHKERRQ(ierr);
  if (!nep->nini) {
    ierr = BVSetRandomColumn(nep->V,0);CHKERRQ(ierr);
    ierr = BVNormColumn(nep->V,0,NORM_2,&nrm);CHKERRQ(ierr);
    ierr = BVScaleColumn(nep->V,0,1.0/nrm);CHKERRQ(ierr);
    n = 1;
  } else n = nep->nini;

  /* build projected matrices for initial space */
  ierr = DSSetDimensions(nep->ds,n,0,0,0);CHKERRQ(ierr);
  ierr = NEPProjectOperator(nep,0,n);CHKERRQ(ierr);

  /* prepare linear solver */
  ierr = NEPComputeFunction(nep,lambda,T,T);CHKERRQ(ierr);
  ierr = MatDuplicate(T,MAT_COPY_VALUES,&Tsigma);CHKERRQ(ierr);
  ierr = KSPSetOperators(nep->ksp,Tsigma,Tsigma);CHKERRQ(ierr);

  /* Restart loop */
  while (nep->reason == NEP_CONVERGED_ITERATING) {
    nep->its++;

    /* solve projected problem */
    ierr = DSSetDimensions(nep->ds,n,0,0,0);CHKERRQ(ierr);
    ierr = DSSetState(nep->ds,DS_STATE_RAW);CHKERRQ(ierr);
    ierr = DSSolve(nep->ds,nep->eigr,NULL);CHKERRQ(ierr);
    lambda = nep->eigr[0];

    /* compute Ritz vector, x = V*s */
    ierr = DSGetArray(nep->ds,DS_MAT_X,&X);CHKERRQ(ierr);
    ierr = BVSetActiveColumns(nep->V,0,n);CHKERRQ(ierr);
    ierr = BVMultVec(nep->V,1.0,0.0,x,X);CHKERRQ(ierr);
    ierr = DSRestoreArray(nep->ds,DS_MAT_X,&X);CHKERRQ(ierr);

    /* compute the residual, r = T(lambda)*x */
    ierr = NEPApplyFunction(nep,lambda,x,w,r,NULL,NULL);CHKERRQ(ierr);

    /* convergence test */
    ierr = VecNorm(r,NORM_2,&resnorm);CHKERRQ(ierr);
    ierr = (*nep->converged)(nep,lambda,0,resnorm,&nep->errest[nep->nconv],nep->convergedctx);CHKERRQ(ierr);
    if (nep->errest[nep->nconv]<=nep->tol) {
      ierr = BVInsertVec(nep->V,nep->nconv,x);CHKERRQ(ierr);
      nep->nconv = nep->nconv + 1;
    }
    ierr = (*nep->stopping)(nep,nep->its,nep->max_it,nep->nconv,nep->nev,&nep->reason,nep->stoppingctx);CHKERRQ(ierr);
    ierr = NEPMonitor(nep,nep->its,nep->nconv,nep->eigr,nep->eigi,nep->errest,1);CHKERRQ(ierr);

    if (nep->reason == NEP_CONVERGED_ITERATING) {

      /* continuation vector: f = T(sigma)\r */
      ierr = BVGetColumn(nep->V,n,&f);CHKERRQ(ierr);
      ierr = NEP_KSPSolve(nep,r,f);CHKERRQ(ierr);
      ierr = BVRestoreColumn(nep->V,n,&f);CHKERRQ(ierr);
      ierr = KSPGetConvergedReason(nep->ksp,&kspreason);CHKERRQ(ierr);
      if (kspreason<0) {
        ierr = PetscInfo1(nep,"iter=%D, linear solve failed, stopping solve\n",nep->its);CHKERRQ(ierr);
        nep->reason = NEP_DIVERGED_LINEAR_SOLVE;
        break;
      }

      /* orthonormalize */
      ierr = BVOrthogonalizeColumn(nep->V,n,NULL,&beta,&breakdown);CHKERRQ(ierr);
      if (breakdown || beta==0.0) {
        ierr = PetscInfo1(nep,"iter=%D, orthogonalization failed, stopping solve\n",nep->its);CHKERRQ(ierr);
        nep->reason = NEP_DIVERGED_BREAKDOWN;
        break;
      }
      ierr = BVScaleColumn(nep->V,n,1.0/beta);CHKERRQ(ierr);

      /* update projected matrices */
      ierr = DSSetDimensions(nep->ds,n+1,0,0,0);CHKERRQ(ierr);
      ierr = NEPProjectOperator(nep,n,n+1);CHKERRQ(ierr);
      n++;
    }
  }
  ierr = MatDestroy(&Tsigma);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "NEPCreate_NArnoldi"
PETSC_EXTERN PetscErrorCode NEPCreate_NArnoldi(NEP nep)
{
  PetscFunctionBegin;
  nep->ops->solve          = NEPSolve_NArnoldi;
  nep->ops->setup          = NEPSetUp_NArnoldi;
  PetscFunctionReturn(0);
}

