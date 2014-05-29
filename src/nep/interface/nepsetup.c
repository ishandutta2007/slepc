/*
      NEP routines related to problem setup.

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

#include <slepc-private/nepimpl.h>       /*I "slepcnep.h" I*/
#include <slepc-private/ipimpl.h>

#undef __FUNCT__
#define __FUNCT__ "NEPSetUp"
/*@
   NEPSetUp - Sets up all the internal data structures necessary for the
   execution of the NEP solver.

   Collective on NEP

   Input Parameter:
.  nep   - solver context

   Notes:
   This function need not be called explicitly in most cases, since NEPSolve()
   calls it. It can be useful when one wants to measure the set-up time
   separately from the solve time.

   Level: advanced

.seealso: NEPCreate(), NEPSolve(), NEPDestroy()
@*/
PetscErrorCode NEPSetUp(NEP nep)
{
  PetscErrorCode ierr;
  PetscInt       k;
  Mat            T;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(nep,NEP_CLASSID,1);
  if (nep->setupcalled) PetscFunctionReturn(0);
  ierr = PetscLogEventBegin(NEP_SetUp,nep,0,0,0);CHKERRQ(ierr);

  /* reset the convergence flag from the previous solves */
  nep->reason = NEP_CONVERGED_ITERATING;

  /* set default solver type (NEPSetFromOptions was not called) */
  if (!((PetscObject)nep)->type_name) {
    ierr = NEPSetType(nep,NEPRII);CHKERRQ(ierr);
  }
  if (!nep->ds) { ierr = NEPGetDS(nep,&nep->ds);CHKERRQ(ierr); }
  ierr = DSReset(nep->ds);CHKERRQ(ierr);
  if (!((PetscObject)nep->rand)->type_name) {
    ierr = PetscRandomSetFromOptions(nep->rand);CHKERRQ(ierr);
  }
  if (!nep->ksp) {
    ierr = NEPGetKSP(nep,&nep->ksp);CHKERRQ(ierr);
  }

  /* by default, compute eigenvalues close to target */
  /* nep->target should contain the initial guess for the eigenvalue */
  if (!nep->which) nep->which = NEP_TARGET_MAGNITUDE;

  /* set problem dimensions */
  if (nep->split) {
    ierr = MatDuplicate(nep->A[0],MAT_DO_NOT_COPY_VALUES,&nep->function);CHKERRQ(ierr);
    ierr = MatDuplicate(nep->A[0],MAT_DO_NOT_COPY_VALUES,&nep->jacobian);CHKERRQ(ierr);
    ierr = PetscLogObjectParent((PetscObject)nep,(PetscObject)nep->function);CHKERRQ(ierr);
    ierr = PetscLogObjectParent((PetscObject)nep,(PetscObject)nep->jacobian);CHKERRQ(ierr);
    ierr = MatGetSize(nep->A[0],&nep->n,NULL);CHKERRQ(ierr);
    ierr = MatGetLocalSize(nep->A[0],&nep->nloc,NULL);CHKERRQ(ierr);
  } else {
    ierr = NEPGetFunction(nep,&T,NULL,NULL,NULL);CHKERRQ(ierr);
    ierr = MatGetSize(T,&nep->n,NULL);CHKERRQ(ierr);
    ierr = MatGetLocalSize(T,&nep->nloc,NULL);CHKERRQ(ierr);
  }

  /* call specific solver setup */
  ierr = (*nep->ops->setup)(nep);CHKERRQ(ierr);

  /* set tolerances if not yet set */
  if (nep->abstol==PETSC_DEFAULT) nep->abstol = 1e-50;
  if (nep->rtol==PETSC_DEFAULT) nep->rtol = 100*SLEPC_DEFAULT_TOL;
  if (nep->stol==PETSC_DEFAULT) nep->stol = SLEPC_DEFAULT_TOL;
  nep->ktol   = 0.1;
  nep->nfuncs = 0;

  /* set eigenvalue comparison */
  switch (nep->which) {
    case NEP_LARGEST_MAGNITUDE:
      nep->comparison    = SlepcCompareLargestMagnitude;
      nep->comparisonctx = NULL;
      break;
    case NEP_SMALLEST_MAGNITUDE:
      nep->comparison    = SlepcCompareSmallestMagnitude;
      nep->comparisonctx = NULL;
      break;
    case NEP_LARGEST_REAL:
      nep->comparison    = SlepcCompareLargestReal;
      nep->comparisonctx = NULL;
      break;
    case NEP_SMALLEST_REAL:
      nep->comparison    = SlepcCompareSmallestReal;
      nep->comparisonctx = NULL;
      break;
    case NEP_LARGEST_IMAGINARY:
      nep->comparison    = SlepcCompareLargestImaginary;
      nep->comparisonctx = NULL;
      break;
    case NEP_SMALLEST_IMAGINARY:
      nep->comparison    = SlepcCompareSmallestImaginary;
      nep->comparisonctx = NULL;
      break;
    case NEP_TARGET_MAGNITUDE:
      nep->comparison    = SlepcCompareTargetMagnitude;
      nep->comparisonctx = &nep->target;
      break;
    case NEP_TARGET_REAL:
      nep->comparison    = SlepcCompareTargetReal;
      nep->comparisonctx = &nep->target;
      break;
    case NEP_TARGET_IMAGINARY:
      nep->comparison    = SlepcCompareTargetImaginary;
      nep->comparisonctx = &nep->target;
      break;
  }

  if (nep->ncv > nep->n) SETERRQ(PetscObjectComm((PetscObject)nep),PETSC_ERR_ARG_OUTOFRANGE,"ncv must be the problem size at most");
  if (nep->nev > nep->ncv) SETERRQ(PetscObjectComm((PetscObject)nep),PETSC_ERR_ARG_OUTOFRANGE,"nev bigger than ncv");

  /* process initial vectors */
  if (nep->nini<0) {
    k = -nep->nini;
    if (k>nep->ncv) SETERRQ(PetscObjectComm((PetscObject)nep),1,"The number of initial vectors is larger than ncv");
    ierr = BVInsertVecs(nep->V,0,&k,nep->IS,PETSC_TRUE);CHKERRQ(ierr);
    ierr = SlepcBasisDestroy_Private(&nep->nini,&nep->IS);CHKERRQ(ierr);
    nep->nini = k;
  }
  ierr = PetscLogEventEnd(NEP_SetUp,nep,0,0,0);CHKERRQ(ierr);
  nep->setupcalled = 1;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "NEPSetInitialSpace"
/*@
   NEPSetInitialSpace - Specify a basis of vectors that constitute the initial
   space, that is, the subspace from which the solver starts to iterate.

   Collective on NEP and Vec

   Input Parameter:
+  nep   - the nonlinear eigensolver context
.  n     - number of vectors
-  is    - set of basis vectors of the initial space

   Notes:
   Some solvers start to iterate on a single vector (initial vector). In that case,
   the other vectors are ignored.

   These vectors do not persist from one NEPSolve() call to the other, so the
   initial space should be set every time.

   The vectors do not need to be mutually orthonormal, since they are explicitly
   orthonormalized internally.

   Common usage of this function is when the user can provide a rough approximation
   of the wanted eigenspace. Then, convergence may be faster.

   Level: intermediate
@*/
PetscErrorCode NEPSetInitialSpace(NEP nep,PetscInt n,Vec *is)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(nep,NEP_CLASSID,1);
  PetscValidLogicalCollectiveInt(nep,n,2);
  if (n<0) SETERRQ(PetscObjectComm((PetscObject)nep),PETSC_ERR_ARG_OUTOFRANGE,"Argument n cannot be negative");
  ierr = SlepcBasisReference_Private(n,is,&nep->nini,&nep->IS);CHKERRQ(ierr);
  if (n>0) nep->setupcalled = 0;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "NEPAllocateSolution"
/*
  NEPAllocateSolution - Allocate memory storage for common variables such
  as eigenvalues and eigenvectors. The argument extra is used for methods
  that require a working basis slightly larger than ncv.
*/
PetscErrorCode NEPAllocateSolution(NEP nep,PetscInt extra)
{
  PetscErrorCode ierr;
  PetscInt       oldsize,newc,requested;
  PetscLogDouble cnt;
  Mat            T;
  Vec            t;

  PetscFunctionBegin;
  requested = nep->ncv + extra;

  /* oldsize is zero if this is the first time setup is called */
  ierr = BVGetSizes(nep->V,NULL,NULL,&oldsize);CHKERRQ(ierr);
  newc = PetscMax(0,requested-oldsize);

  /* allocate space for eigenvalues and friends */
  if (requested != oldsize) {
    if (oldsize) {
      ierr = PetscFree3(nep->eig,nep->errest,nep->perm);CHKERRQ(ierr);
    }
    ierr = PetscMalloc3(requested,&nep->eig,requested,&nep->errest,requested,&nep->perm);CHKERRQ(ierr);
    cnt = newc*sizeof(PetscScalar) + newc*sizeof(PetscReal) + newc*sizeof(PetscInt);
    ierr = PetscLogObjectMemory((PetscObject)nep,cnt);CHKERRQ(ierr);
  }

  /* allocate V */
  if (!nep->V) { ierr = NEPGetBV(nep,&nep->V);CHKERRQ(ierr); }
  if (!oldsize) {
    if (!((PetscObject)(nep->V))->type_name) {
      ierr = BVSetType(nep->V,BVSVEC);CHKERRQ(ierr);
    }
    if (nep->split) T = nep->A[0];
    else {
      ierr = NEPGetFunction(nep,&T,NULL,NULL,NULL);CHKERRQ(ierr);
    }
    ierr = MatGetVecs(T,&t,NULL);CHKERRQ(ierr);
    ierr = BVSetSizesFromVec(nep->V,t,requested);CHKERRQ(ierr);
    ierr = VecDestroy(&t);CHKERRQ(ierr);
  } else {
    ierr = BVResize(nep->V,requested,PETSC_FALSE);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

