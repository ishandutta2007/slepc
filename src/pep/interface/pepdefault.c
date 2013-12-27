/*
     This file contains some simple default routines for common PEP operations.

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

#include <slepc-private/pepimpl.h>     /*I "slepcpep.h" I*/

#undef __FUNCT__
#define __FUNCT__ "PEPReset_Default"
PetscErrorCode PEPReset_Default(PEP pep)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = VecDestroyVecs(pep->nwork,&pep->work);CHKERRQ(ierr);
  pep->nwork = 0;
  ierr = PEPFreeSolution(pep);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PEPSetWorkVecs"
/*@
   PEPSetWorkVecs - Sets a number of work vectors into a PEP object

   Collective on PEP

   Input Parameters:
+  pep - polynomial eigensolver context
-  nw  - number of work vectors to allocate

   Developers Note:
   This is PETSC_EXTERN because it may be required by user plugin PEP
   implementations.

   Level: developer
@*/
PetscErrorCode PEPSetWorkVecs(PEP pep,PetscInt nw)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (pep->nwork != nw) {
    ierr = VecDestroyVecs(pep->nwork,&pep->work);CHKERRQ(ierr);
    pep->nwork = nw;
    ierr = VecDuplicateVecs(pep->t,nw,&pep->work);CHKERRQ(ierr);
    ierr = PetscLogObjectParents(pep,nw,pep->work);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PEPConvergedEigRelative"
/*
  PEPConvergedEigRelative - Checks convergence relative to the eigenvalue.
*/
PetscErrorCode PEPConvergedEigRelative(PEP pep,PetscScalar eigr,PetscScalar eigi,PetscReal res,PetscReal *errest,void *ctx)
{
  PetscReal w;

  PetscFunctionBegin;
  w = SlepcAbsEigenvalue(eigr,eigi);
  *errest = res/w;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PEPConvergedAbsolute"
/*
  PEPConvergedAbsolute - Checks convergence absolutely.
*/
PetscErrorCode PEPConvergedAbsolute(PEP pep,PetscScalar eigr,PetscScalar eigi,PetscReal res,PetscReal *errest,void *ctx)
{
  PetscFunctionBegin;
  *errest = res;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PEPComputeVectors_Schur"
PetscErrorCode PEPComputeVectors_Schur(PEP pep)
{
  PetscErrorCode ierr;
  PetscInt       n,ld,i;
  PetscScalar    *Z;
#if !defined(PETSC_USE_COMPLEX)
  PetscScalar    tmp;
  PetscReal      norm,normi;
#endif

  PetscFunctionBegin;
  ierr = DSGetLeadingDimension(pep->ds,&ld);CHKERRQ(ierr);
  ierr = DSGetDimensions(pep->ds,&n,NULL,NULL,NULL,NULL);CHKERRQ(ierr);

  /* right eigenvectors */
  ierr = DSVectors(pep->ds,DS_MAT_X,NULL,NULL);CHKERRQ(ierr);

  /* AV = V * Z */
  ierr = DSGetArray(pep->ds,DS_MAT_X,&Z);CHKERRQ(ierr);
  ierr = SlepcUpdateVectors(n,pep->V,0,n,Z,ld,PETSC_FALSE);CHKERRQ(ierr);
  ierr = DSRestoreArray(pep->ds,DS_MAT_X,&Z);CHKERRQ(ierr);

  /* Fix eigenvectors if balancing was used */
  if (pep->balance && pep->Dr) {
    for (i=0;i<n;i++) {
      ierr = VecPointwiseMult(pep->V[i],pep->V[i],pep->Dr);CHKERRQ(ierr);
    }
  }

  /* normalization */
  for (i=0;i<n;i++) {
#if !defined(PETSC_USE_COMPLEX)
    if (pep->eigi[i] != 0.0) {
      ierr = VecNorm(pep->V[i],NORM_2,&norm);CHKERRQ(ierr);
      ierr = VecNorm(pep->V[i+1],NORM_2,&normi);CHKERRQ(ierr);
      tmp = 1.0 / SlepcAbsEigenvalue(norm,normi);
      ierr = VecScale(pep->V[i],tmp);CHKERRQ(ierr);
      ierr = VecScale(pep->V[i+1],tmp);CHKERRQ(ierr);
      i++;
    } else
#endif
    {
      ierr = VecNormalize(pep->V[i],NULL);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "PEPKrylovConvergence"
/*
   PEPKrylovConvergence - This is the analogue to EPSKrylovConvergence, but
   for polynomial Krylov methods.

   Differences:
   - Always non-symmetric
   - Does not check for STSHIFT
   - No correction factor
   - No support for true residual
*/
PetscErrorCode PEPKrylovConvergence(PEP pep,PetscBool getall,PetscInt kini,PetscInt nits,PetscInt nv,PetscReal beta,PetscInt *kout)
{
  PetscErrorCode ierr;
  PetscInt       k,newk,marker,ld;
  PetscScalar    re,im;
  PetscReal      resnorm;

  PetscFunctionBegin;
  ierr = DSGetLeadingDimension(pep->ds,&ld);CHKERRQ(ierr);
  marker = -1;
  if (pep->trackall) getall = PETSC_TRUE;
  for (k=kini;k<kini+nits;k++) {
    /* eigenvalue */
    re = pep->eigr[k];
    im = pep->eigi[k];
    newk = k;
    ierr = DSVectors(pep->ds,DS_MAT_X,&newk,&resnorm);CHKERRQ(ierr);
    resnorm *= beta;
    /* error estimate */
    ierr = (*pep->converged)(pep,re,im,resnorm,&pep->errest[k],pep->convergedctx);CHKERRQ(ierr);
    if (marker==-1 && pep->errest[k] >= pep->tol) marker = k;
    if (newk==k+1) {
      pep->errest[k+1] = pep->errest[k];
      k++;
    }
    if (marker!=-1 && !getall) break;
  }
  if (marker!=-1) k = marker;
  *kout = k;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "PEPBuildBalance"
/*
  PEPBuildBalance compute two diagonal matrices to be applied for balancing in plynomial eigenproblems.
*/
PetscErrorCode PEPBuildBalance(PEP pep)
{
  PetscErrorCode ierr;
  PetscInt       it,i,j,k,nmat,nr,e,nz,lst,lend,nc=0,*cols;
  const PetscInt *cidx,*ridx;
  Mat            M,*T,A;
  PetscMPIInt    emax,emin,emaxl,eminl,n;
  PetscBool      cont=PETSC_TRUE,flg=PETSC_FALSE;
  PetscScalar    *array,*Dr,*Dl,t;
  PetscReal      l2,d,*rsum,*aux,*csum,w=pep->balance_w;
  MatStructure   str;
  MatInfo        info;

  PetscFunctionBegin;
  l2 = 2*PetscLogReal(2.0);
  nmat = pep->nmat;
  ierr = PetscMPIIntCast(pep->n,&n);
  ierr = STGetMatStructure(pep->st,&str);CHKERRQ(ierr);
  ierr = PetscMalloc1(nmat,&T);CHKERRQ(ierr);
  for (k=0;k<nmat;k++) {
    ierr = STGetTOperators(pep->st,k,&T[k]);CHKERRQ(ierr);
  }
  /* Form local auxiliar matrix M */
  ierr = PetscObjectTypeCompareAny((PetscObject)T[0],&cont,MATMPIAIJ,MATSEQAIJ);CHKERRQ(ierr);
  if (!cont) SETERRQ(PetscObjectComm((PetscObject)T[0]), PETSC_ERR_SUP,"Only for MPIAIJ or SEQAIJ matrix types");
  ierr = PetscObjectTypeCompare((PetscObject)T[0],MATMPIAIJ,&cont);CHKERRQ(ierr);
  if (cont) {
    ierr = MatMPIAIJGetLocalMat(T[0],MAT_INITIAL_MATRIX,&M);CHKERRQ(ierr);
    flg = PETSC_TRUE; 
  } else {
    ierr = MatDuplicate(T[0],MAT_COPY_VALUES,&M);CHKERRQ(ierr);
  }
  ierr = MatGetInfo(M,MAT_LOCAL,&info);CHKERRQ(ierr);
  nz = info.nz_used;
  ierr = MatSeqAIJGetArray(M,&array);CHKERRQ(ierr);
  for (i=0;i<nz;i++) {
    t = PetscAbsScalar(array[i]);
    array[i] = t*t;
  }
  ierr = MatSeqAIJRestoreArray(M,&array);CHKERRQ(ierr);
  for (k=1;k<nmat;k++) {
    if (flg) {
      ierr = MatMPIAIJGetLocalMat(T[k],MAT_INITIAL_MATRIX,&A);CHKERRQ(ierr);
    } else {
      if (str==SAME_NONZERO_PATTERN){
        ierr = MatCopy(T[k],A,SAME_NONZERO_PATTERN);CHKERRQ(ierr);
      } else {
        ierr = MatDuplicate(T[k],MAT_COPY_VALUES,&A);CHKERRQ(ierr);
      }
    }
    ierr = MatGetInfo(A,MAT_LOCAL,&info);CHKERRQ(ierr);
    nz = info.nz_used;
    ierr = MatSeqAIJGetArray(A,&array);CHKERRQ(ierr);
    for (i=0;i<nz;i++) {
      t = PetscAbsScalar(array[i]);
      array[i] = t*t;
    }
    ierr = MatSeqAIJRestoreArray(A,&array);CHKERRQ(ierr);
    ierr = MatAXPY(M,w*w,A,str);CHKERRQ(ierr);
    if (flg || str!=SAME_NONZERO_PATTERN || k==nmat-2) {
      ierr = MatDestroy(&A);CHKERRQ(ierr);
    } 
    w *= pep->balance_w;
  }
  ierr = MatGetRowIJ(M,0,PETSC_FALSE,PETSC_FALSE,&nr,&ridx,&cidx,&cont);CHKERRQ(ierr);
  if (!cont) SETERRQ(PetscObjectComm((PetscObject)T[0]), PETSC_ERR_SUP,"It is not possible to compute scaling diagonals to balance the PEP matrices");
  ierr = MatGetInfo(M,MAT_LOCAL,&info);CHKERRQ(ierr);
  nz = info.nz_used;
  ierr = VecGetOwnershipRange(pep->Dl,&lst,&lend);CHKERRQ(ierr);
  ierr = PetscMalloc4(nr,&rsum,pep->n,&csum,pep->n,&aux,PetscMin(pep->n-lend+lst,nz),&cols);CHKERRQ(ierr);
  ierr = VecSet(pep->Dr,1.0);CHKERRQ(ierr);
  ierr = VecSet(pep->Dl,1.0);CHKERRQ(ierr);
  ierr = VecGetArray(pep->Dl,&Dl);CHKERRQ(ierr);
  ierr = VecGetArray(pep->Dr,&Dr);CHKERRQ(ierr);
  ierr = MatSeqAIJGetArray(M,&array);CHKERRQ(ierr);
  ierr = PetscMemzero(aux,pep->n*sizeof(PetscReal));CHKERRQ(ierr);
  for (j=0;j<nz;j++) {
    /* Search non-zero columns outsize lst-lend */
    if ( aux[cidx[j]]==0 && (cidx[j]<lst || lend<=cidx[j]) ) cols[nc++] = cidx[j];
    /* Local column sums */
    aux[cidx[j]] += PetscAbsScalar(array[j]);
  }
  for (it=0;it<pep->balance_its && cont;it++) {
    emaxl = 0; eminl = 0;
    /* Columns' sum  */    
    if (it>0) { /* it=0 has been already done*/
      ierr = MatSeqAIJGetArray(M,&array);CHKERRQ(ierr);
      ierr = PetscMemzero(aux,pep->n*sizeof(PetscReal));CHKERRQ(ierr);
      for (j=0;j<nz;j++) aux[cidx[j]] += PetscAbsScalar(array[j]);
      ierr = MatSeqAIJRestoreArray(M,&array);CHKERRQ(ierr); 
    }
    ierr = MPI_Allreduce(aux,csum,n,MPIU_REAL,MPI_SUM,PetscObjectComm((PetscObject)pep->Dr));
    /* Update Dr */
    for (j=lst;j<lend;j++) {
      d = PetscLogReal(csum[j])/l2;
      e = -(PetscInt)((d < 0)?(d-0.5):(d+0.5));
      d = PetscPowReal(2,e);
      Dr[j-lst] *= d;
      aux[j] = d*d;
      emaxl = PetscMax(emaxl,e);
      eminl = PetscMin(eminl,e);
    }
    for (j=0;j<nc;j++) {
      d = PetscLogReal(csum[cols[j]])/l2;
      e = -(PetscInt)((d < 0)?(d-0.5):(d+0.5));
      d = PetscPowReal(2,e);
      aux[cols[j]] = d*d;
      emaxl = PetscMax(emaxl,e);
      eminl = PetscMin(eminl,e);
    }
    /* Scale M */
    ierr = MatSeqAIJGetArray(M,&array);CHKERRQ(ierr);
    for (j=0;j<nz;j++) {
      array[j] *= aux[cidx[j]];
    }
    ierr = MatSeqAIJRestoreArray(M,&array);CHKERRQ(ierr);
    /* Row sum  */    
    ierr = PetscMemzero(rsum,nr*sizeof(PetscReal));CHKERRQ(ierr);
    ierr = MatSeqAIJGetArray(M,&array);CHKERRQ(ierr);
    for (i=0;i<nr;i++) {
      for (j=ridx[i];j<ridx[i+1];j++) rsum[i] += PetscAbsScalar(array[j]);
      /* Update Dl */
      d = PetscLogReal(rsum[i])/l2;
      e = -(PetscInt)((d < 0)?(d-0.5):(d+0.5));
      d = PetscPowReal(2,e);
      Dl[i] *= d;
      /* Scale M */
      for (j=ridx[i];j<ridx[i+1];j++) array[j] *= d*d;
      emaxl = PetscMax(emaxl,e);
      eminl = PetscMin(eminl,e);      
    }
    ierr = MatSeqAIJRestoreArray(M,&array);CHKERRQ(ierr);  
    /* Compute global max and min */
    ierr = MPI_Allreduce(&emaxl,&emax,1,MPIU_INT,MPI_MAX,PetscObjectComm((PetscObject)pep->Dl));
    ierr = MPI_Allreduce(&eminl,&emin,1,MPIU_INT,MPI_MIN,PetscObjectComm((PetscObject)pep->Dl));
    if (emax<=emin+2) cont = PETSC_FALSE;
  }
  ierr = VecRestoreArray(pep->Dr,&Dr);CHKERRQ(ierr);
  ierr = VecRestoreArray(pep->Dl,&Dl);CHKERRQ(ierr);
  /* Free memory*/
  ierr = MatDestroy(&M);CHKERRQ(ierr);
  ierr = PetscFree4(rsum,csum,aux,cols);CHKERRQ(ierr);
  ierr = PetscFree(T);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
