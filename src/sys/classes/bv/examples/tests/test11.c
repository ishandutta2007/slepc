/*
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   SLEPc - Scalable Library for Eigenvalue Problem Computations
   Copyright (c) 2002-2017, Universitat Politecnica de Valencia, Spain

   This file is part of SLEPc.
   SLEPc is distributed under a 2-clause BSD license (see LICENSE).
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
*/

static char help[] = "Test BV block orthogonalization.\n\n";

#include <slepcbv.h>

int main(int argc,char **argv)
{
  PetscErrorCode    ierr;
  BV                X,Y,Z,cached;
  Mat               B,M,R=NULL;
  Vec               v,t;
  PetscInt          i,j,n=20,l=2,k=8,Istart,Iend;
  PetscViewer       view;
  PetscBool         withb,resid,verbose;
  PetscReal         norm;
  PetscScalar       alpha;
  BVOrthogBlockType btype;

  ierr = SlepcInitialize(&argc,&argv,(char*)0,help);if (ierr) return ierr;
  ierr = PetscOptionsGetInt(NULL,NULL,"-n",&n,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetInt(NULL,NULL,"-l",&l,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetInt(NULL,NULL,"-k",&k,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsHasName(NULL,NULL,"-withb",&withb);CHKERRQ(ierr);
  ierr = PetscOptionsHasName(NULL,NULL,"-resid",&resid);CHKERRQ(ierr);
  ierr = PetscOptionsHasName(NULL,NULL,"-verbose",&verbose);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Test BV block orthogonalization (length %D, l=%D, k=%D)%s.\n",n,l,k,withb?" with non-standard inner product":"");CHKERRQ(ierr);

  l = 0;  /* temporarily ignore leading columns */

  /* Create template vector */
  ierr = VecCreate(PETSC_COMM_WORLD,&t);CHKERRQ(ierr);
  ierr = VecSetSizes(t,PETSC_DECIDE,n);CHKERRQ(ierr);
  ierr = VecSetFromOptions(t);CHKERRQ(ierr);

  /* Create BV object X */
  ierr = BVCreate(PETSC_COMM_WORLD,&X);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)X,"X");CHKERRQ(ierr);
  ierr = BVSetSizesFromVec(X,t,k);CHKERRQ(ierr);
  ierr = BVSetFromOptions(X);CHKERRQ(ierr);
  ierr = BVGetOrthogonalization(X,NULL,NULL,NULL,&btype);CHKERRQ(ierr);

  /* Set up viewer */
  ierr = PetscViewerASCIIGetStdout(PETSC_COMM_WORLD,&view);CHKERRQ(ierr);
  if (verbose) {
    ierr = PetscViewerPushFormat(view,PETSC_VIEWER_ASCII_MATLAB);CHKERRQ(ierr);
  }

  /* Fill X entries */
  for (j=0;j<k;j++) {
    ierr = BVGetColumn(X,j,&v);CHKERRQ(ierr);
    ierr = VecSet(v,0.0);CHKERRQ(ierr);
    for (i=0;i<=n/2;i++) {
      if (i+j<n) {
        alpha = (3.0*i+j-2)/(2*(i+j+1));
        ierr = VecSetValue(v,i+j,alpha,INSERT_VALUES);CHKERRQ(ierr);
      }
    }
    ierr = VecAssemblyBegin(v);CHKERRQ(ierr);
    ierr = VecAssemblyEnd(v);CHKERRQ(ierr);
    ierr = BVRestoreColumn(X,j,&v);CHKERRQ(ierr);
  }
  if (btype==BV_ORTHOG_BLOCK_GS) {  /* GS requires the leading columns to be orthogonal */
    for (j=0;j<l;j++) {
      ierr = BVOrthonormalizeColumn(X,j,PETSC_FALSE,NULL,NULL);CHKERRQ(ierr);
    }
  }
  if (verbose) {
    ierr = BVView(X,view);CHKERRQ(ierr);
  }

  if (withb) {
    /* Create inner product matrix */
    ierr = MatCreate(PETSC_COMM_WORLD,&B);CHKERRQ(ierr);
    ierr = MatSetSizes(B,PETSC_DECIDE,PETSC_DECIDE,n,n);CHKERRQ(ierr);
    ierr = MatSetFromOptions(B);CHKERRQ(ierr);
    ierr = MatSetUp(B);CHKERRQ(ierr);
    ierr = PetscObjectSetName((PetscObject)B,"B");CHKERRQ(ierr);

    ierr = MatGetOwnershipRange(B,&Istart,&Iend);CHKERRQ(ierr);
    for (i=Istart;i<Iend;i++) {
      if (i>0) { ierr = MatSetValue(B,i,i-1,-1.0,INSERT_VALUES);CHKERRQ(ierr); }
      if (i<n-1) { ierr = MatSetValue(B,i,i+1,-1.0,INSERT_VALUES);CHKERRQ(ierr); }
      ierr = MatSetValue(B,i,i,2.0,INSERT_VALUES);CHKERRQ(ierr);
    }
    ierr = MatAssemblyBegin(B,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    ierr = MatAssemblyEnd(B,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
    if (verbose) {
      ierr = MatView(B,view);CHKERRQ(ierr);
    }
    ierr = BVSetMatrix(X,B,PETSC_FALSE);CHKERRQ(ierr);
  }

  /* Create copy on Y */
  ierr = BVDuplicate(X,&Y);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)Y,"Y");CHKERRQ(ierr);
  ierr = BVCopy(X,Y);CHKERRQ(ierr);
  ierr = BVSetActiveColumns(Y,l,k);CHKERRQ(ierr);
  ierr = BVSetActiveColumns(X,l,k);CHKERRQ(ierr);
  if (btype==BV_ORTHOG_BLOCK_GS) {  /* GS requires the leading columns to be orthogonal */
    for (j=0;j<l;j++) {
      ierr = BVOrthonormalizeColumn(Y,j,PETSC_FALSE,NULL,NULL);CHKERRQ(ierr);
    }
  }

  if (resid) {
    /* Create matrix R to store triangular factor */
    ierr = MatCreateSeqDense(PETSC_COMM_SELF,k,k,NULL,&R);CHKERRQ(ierr);
    ierr = PetscObjectSetName((PetscObject)R,"R");CHKERRQ(ierr);
  }

  /* Test BVOrthogonalize */
  ierr = BVOrthogonalize(Y,R);CHKERRQ(ierr);
  if (verbose) {
    ierr = BVView(Y,view);CHKERRQ(ierr);
    if (resid) { ierr = MatView(R,view);CHKERRQ(ierr); }
  }

  if (withb) {
    /* Extract cached BV and check it is equal to B*X */
    ierr = BVGetCachedBV(Y,&cached);CHKERRQ(ierr);
    ierr = BVDuplicate(X,&Z);CHKERRQ(ierr);
    ierr = BVSetMatrix(Z,NULL,PETSC_FALSE);CHKERRQ(ierr);
    ierr = BVSetActiveColumns(Z,l,k);CHKERRQ(ierr);
    ierr = BVMatMult(X,B,Z);CHKERRQ(ierr);
    ierr = BVMult(Z,-1.0,1.0,cached,NULL);CHKERRQ(ierr);
    ierr = BVNorm(Z,NORM_FROBENIUS,&norm);CHKERRQ(ierr);
    if (norm<100*PETSC_MACHINE_EPSILON) {
      ierr = PetscPrintf(PETSC_COMM_WORLD,"Residual ||cached-BX|| < 100*eps\n");CHKERRQ(ierr);
    } else {
      ierr = PetscPrintf(PETSC_COMM_WORLD,"Residual ||cached-BX||: %g\n",(double)norm);CHKERRQ(ierr);
    }
    ierr = BVDestroy(&Z);CHKERRQ(ierr);
    ierr = MatDestroy(&B);CHKERRQ(ierr);
  }

  /* Check orthogonality */
  ierr = MatCreateSeqDense(PETSC_COMM_SELF,k,k,NULL,&M);CHKERRQ(ierr);
  ierr = MatShift(M,1.0);CHKERRQ(ierr);   /* set leading part to identity */
  ierr = BVDot(Y,Y,M);CHKERRQ(ierr);
  ierr = MatShift(M,-1.0);CHKERRQ(ierr);
  ierr = MatNorm(M,NORM_1,&norm);CHKERRQ(ierr);
  if (norm<100*PETSC_MACHINE_EPSILON) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Level of orthogonality < 100*eps\n");CHKERRQ(ierr);
  } else {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Level of orthogonality: %g\n",(double)norm);CHKERRQ(ierr);
  }

  if (resid) {
    /* Check residual */
    ierr = BVMult(X,-1.0,1.0,Y,R);CHKERRQ(ierr);
    ierr = BVSetMatrix(X,NULL,PETSC_FALSE);CHKERRQ(ierr);
    ierr = BVNorm(X,NORM_FROBENIUS,&norm);CHKERRQ(ierr);
    if (norm<100*PETSC_MACHINE_EPSILON) {
      ierr = PetscPrintf(PETSC_COMM_WORLD,"Residual ||X-QR|| < 100*eps\n");CHKERRQ(ierr);
    } else {
      ierr = PetscPrintf(PETSC_COMM_WORLD,"Residual ||X-QR||: %g\n",(double)norm);CHKERRQ(ierr);
    }
    ierr = MatDestroy(&R);CHKERRQ(ierr);
  }

  ierr = MatDestroy(&M);CHKERRQ(ierr);
  ierr = BVDestroy(&X);CHKERRQ(ierr);
  ierr = BVDestroy(&Y);CHKERRQ(ierr);
  ierr = VecDestroy(&t);CHKERRQ(ierr);
  ierr = SlepcFinalize();
  return ierr;
}
