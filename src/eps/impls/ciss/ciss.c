/*

   SLEPc eigensolver: "ciss"

   Method: Contour Integral Spectral Slicing

   Algorithm:

       Contour integral based on Sakurai-Sugiura method to construct a
       subspace, with various eigenpair extractions (Rayleigh-Ritz,
       explicit moment).

   Based on code contributed by Tetsuya Sakurai.

   References:

       [1] T. Sakurai and H. Sugiura, "A projection method for generalized
           eigenvalue problems", J. Comput. Appl. Math. 159:119-128, 2003.

       [2] T. Sakurai and H. Tadano, "CIRR: a Rayleigh-Ritz type method with
           contour integral for generalized eigenvalue problems", Hokkaido
           Math. J. 36:745-757, 2007.

   Last update: Jun 2013

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

#include <slepc-private/epsimpl.h>                /*I "slepceps.h" I*/
#include <slepcblaslapack.h>

PetscErrorCode EPSSolve_CISS(EPS);

typedef struct {
  /* parameters */
  PetscScalar center;     /* center of the region where to find eigenpairs (default: 0.0) */
  PetscReal   radius;     /* radius of the region (1.0) */
  PetscReal   vscale;     /* vertical scale of the region (1.0; 0.1 if spectrum real) */
  PetscInt    N;          /* number of integration points (32) */
  PetscInt    L;          /* block size (16) */
  PetscInt    M;          /* moment degree (N/4 = 4) */
  PetscReal   delta;      /* threshold of singular value (1e-12) */
  PetscInt    npart;      /* number of partitions of the matrix (1) */
  PetscReal   *sigma;     /* threshold for numerical rank */
  PetscInt    L_max;      /* maximum number of columns of the source matrix V */
  PetscReal   spurious_threshold; /* discard spurious eigenpairs */
  PetscBool   isreal;     /* A and B are real */
  PetscInt    refine_inner;
  PetscInt    refine_outer;
  PetscInt    refine_blocksize;
  /* private data */
  PetscInt    solver_comm_id;
  PetscInt    num_solve_point;
  PetscScalar *weight;
  PetscScalar *omega;
  PetscScalar *pp;
  Vec         *V;
  Vec         *Y;
  Vec         *S;
  KSP         *ksp;
  PetscBool   useconj;
  PetscReal   est_eig;
} EPS_CISS;

#undef __FUNCT__
#define __FUNCT__ "SetSolverComm"
static PetscErrorCode SetSolverComm(EPS eps)
{
  EPS_CISS *ctx = (EPS_CISS*)eps->data;
  PetscInt N = ctx->N;

  PetscFunctionBegin;
  if (ctx->useconj) N = N/2;
  ctx->solver_comm_id = 0;
  ctx->num_solve_point = N;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SetPathParameter"
static PetscErrorCode SetPathParameter(EPS eps)
{
  EPS_CISS  *ctx = (EPS_CISS*)eps->data;
  PetscInt  i;
  PetscReal theta;

  PetscFunctionBegin;
  for (i=0;i<ctx->N;i++){
    theta = ((2*PETSC_PI)/ctx->N)*(i+0.5);
    ctx->pp[i] = cos(theta) + PETSC_i*ctx->vscale*sin(theta);
    ctx->omega[i] = ctx->center + ctx->radius*ctx->pp[i];
    ctx->weight[i] = ctx->vscale*cos(theta) + PETSC_i*sin(theta);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "CISSVecSetRandom"
static PetscErrorCode CISSVecSetRandom(Vec x,PetscRandom rctx)
{
  PetscErrorCode ierr;
  PetscInt       j,nlocal;
  PetscScalar    *vdata;

  PetscFunctionBegin;
  ierr = SlepcVecSetRandom(x,rctx);CHKERRQ(ierr);
  ierr = VecGetLocalSize(x,&nlocal);CHKERRQ(ierr);
  ierr = VecGetArray(x,&vdata);CHKERRQ(ierr);
  for (j=0;j<nlocal;j++) {
    vdata[j] = PetscRealPart(vdata[j]);
    if (PetscRealPart(vdata[j]) < 0.5) vdata[j] = -1.0;
    else vdata[j] = 1.0;
  }
  ierr = VecRestoreArray(x,&vdata);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SolveLinearSystem"
static PetscErrorCode SolveLinearSystem(EPS eps)
{
  PetscErrorCode ierr;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;
  PetscInt       i,j,nmat,p_id;
  Mat            A,B,Fz;
  PC             pc;
  Vec            BV;

  PetscFunctionBegin;
  ierr = STGetNumMatrices(eps->st,&nmat);CHKERRQ(ierr);
  ierr = STGetOperators(eps->st,0,&A);CHKERRQ(ierr);
  if (nmat>1) { ierr = STGetOperators(eps->st,1,&B);CHKERRQ(ierr); }
  else B = NULL;
  ierr = MatDuplicate(A,MAT_DO_NOT_COPY_VALUES,&Fz);CHKERRQ(ierr);
  ierr = VecDuplicate(ctx->V[0],&BV);CHKERRQ(ierr);

  for (i=0;i<ctx->num_solve_point;i++) {
    p_id = ctx->solver_comm_id * ctx->num_solve_point + i;
    ierr = MatCopy(A,Fz,DIFFERENT_NONZERO_PATTERN);CHKERRQ(ierr);
    if (nmat>1) {
      ierr = MatAXPY(Fz,-ctx->omega[p_id],B,DIFFERENT_NONZERO_PATTERN);CHKERRQ(ierr);
    } else {
      ierr = MatShift(Fz,-ctx->omega[p_id]);CHKERRQ(ierr);
    }
    ierr = KSPSetOperators(ctx->ksp[i],Fz,Fz,SAME_NONZERO_PATTERN);CHKERRQ(ierr);
    ierr = KSPSetType(ctx->ksp[i],KSPPREONLY);CHKERRQ(ierr);
    ierr = KSPGetPC(ctx->ksp[i],&pc);CHKERRQ(ierr);
    ierr = PCSetType(pc,PCREDUNDANT);CHKERRQ(ierr);
    ierr = KSPSetFromOptions(ctx->ksp[i]);CHKERRQ(ierr);
    for (j=0;j<ctx->L;j++) {
      ierr = VecDuplicate(ctx->V[0],&ctx->Y[i*ctx->L_max+j]);CHKERRQ(ierr);
      ierr = PetscLogObjectParent((PetscObject)eps,(PetscObject)ctx->Y[i*ctx->L_max+j]);CHKERRQ(ierr);
      if (nmat==2) {
        ierr = MatMult(B,ctx->V[j],BV);CHKERRQ(ierr);
        ierr = KSPSolve(ctx->ksp[i],BV,ctx->Y[i*ctx->L_max+j]);CHKERRQ(ierr);
      } else {
        ierr = KSPSolve(ctx->ksp[i],ctx->V[j],ctx->Y[i*ctx->L_max+j]);CHKERRQ(ierr);
      }
    }
  }
  ierr = MatDestroy(&Fz);CHKERRQ(ierr);
  ierr = VecDestroy(&BV);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "ConstructS"
static PetscErrorCode ConstructS(EPS eps,PetscInt M,Vec **S)
{
  PetscErrorCode ierr;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;
  PetscInt       i,j,k;
  Vec            v;
  PetscScalar    *ppk;

  PetscFunctionBegin;
  ierr = VecDuplicateVecs(ctx->Y[0],M*ctx->L,S);CHKERRQ(ierr);
  ierr = PetscMalloc1(ctx->num_solve_point,&ppk);CHKERRQ(ierr);
  for (i=0;i<ctx->num_solve_point;i++) ppk[i] = 1;
  ierr = VecDuplicate(ctx->Y[0],&v);CHKERRQ(ierr);
  for (k=0;k<M;k++) {
    for (j=0;j<ctx->L;j++) {
      ierr = VecSet(v,0);CHKERRQ(ierr);
      for (i=0;i<ctx->num_solve_point; i++) {
        ierr = VecAXPY(v,ppk[i]*ctx->weight[ctx->solver_comm_id*ctx->num_solve_point+i]/(PetscReal)ctx->N,ctx->Y[i*ctx->L_max+j]);CHKERRQ(ierr);
      }
      ierr = VecCopy(v,(*S)[k*ctx->L+j]);CHKERRQ(ierr);
    }
    for (i=0;i<ctx->num_solve_point;i++) {
      ppk[i] *= ctx->pp[ctx->solver_comm_id*ctx->num_solve_point+i];
    }
  }
  ierr = PetscFree(ppk);CHKERRQ(ierr);
  ierr = VecDestroy(&v);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EstimateNumberEigs"
static PetscErrorCode EstimateNumberEigs(EPS eps,Vec *S1,PetscInt *L_add)
{
  PetscErrorCode ierr;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;
  PetscInt       i,j,istart,p_start,p_end;
  PetscScalar    *data,*p_data,tmp,sum = 0.0;
  Vec            V_p;
  PetscReal      eta;

  PetscFunctionBegin;
  ierr = VecGetOwnershipRange(ctx->V[0],&istart,NULL);CHKERRQ(ierr);
  ierr = VecGetOwnershipRange(S1[0],&p_start,&p_end);CHKERRQ(ierr);

  ierr = VecDuplicate(S1[0],&V_p);CHKERRQ(ierr);
  for (i=0;i<ctx->L;i++) {
    ierr = VecGetArray(ctx->V[i],&data);CHKERRQ(ierr);
    ierr = VecGetArray(V_p,&p_data);CHKERRQ(ierr);
    for (j=p_start;j<p_end;j++) p_data[j-p_start] = data[j-istart];
    ierr = VecRestoreArray(ctx->V[i],&data);CHKERRQ(ierr);
    ierr = VecRestoreArray(V_p,&p_data);CHKERRQ(ierr);
    ierr = VecDot(V_p,S1[i],&tmp);CHKERRQ(ierr);
    sum += tmp;
  }
  ierr = VecDestroy(&V_p);CHKERRQ(ierr);
  ctx->est_eig = PetscAbsScalar(ctx->radius*sum/(PetscReal)ctx->L);
  eta = PetscPowReal(10,-log10(eps->tol)/ctx->N);
  ierr = PetscInfo1(eps,"Estimation_#Eig %F\n",ctx->est_eig);CHKERRQ(ierr);
  *L_add = (PetscInt)ceil((ctx->est_eig*eta)/ctx->M) - ctx->L;
  if (*L_add < 0) *L_add = 0;
  if (*L_add>ctx->L_max-ctx->L) {
    ierr = PetscInfo(eps,"Number of eigenvalues around the contour path may be too large\n");
    *L_add = ctx->L_max-ctx->L;
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SetAddVector"
static PetscErrorCode SetAddVector(EPS eps,PetscInt Ladd_end)
{
  PetscErrorCode ierr;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;
  PetscInt       i,j,nlocal,Ladd_start=ctx->L;
  Vec            *newV;
  PetscScalar    *vdata;

  PetscFunctionBegin;
  ierr = PetscMalloc1(Ladd_end,&newV);CHKERRQ(ierr);
  for (i=0;i<ctx->L;i++) { newV[i] = ctx->V[i]; }
  ierr = PetscFree(ctx->V);CHKERRQ(ierr);
  ctx->V = newV;
  ierr = VecGetLocalSize(ctx->V[0],&nlocal);CHKERRQ(ierr);
  for (i=Ladd_start;i<Ladd_end;i++) {
    ierr = VecDuplicate(ctx->V[0],&ctx->V[i]);CHKERRQ(ierr);
    ierr = PetscLogObjectParent((PetscObject)eps,(PetscObject)ctx->V[i]);CHKERRQ(ierr);
    ierr = CISSVecSetRandom(ctx->V[i],eps->rand);CHKERRQ(ierr);
    ierr = VecGetArray(ctx->V[i],&vdata);CHKERRQ(ierr);
    for (j=0;j<nlocal;j++) {
      vdata[j] = PetscRealPart(vdata[j]);
      if (PetscRealPart(vdata[j]) < 0.5) vdata[j] = -1.0;
      else vdata[j] = 1.0;
    }
    ierr = VecRestoreArray(ctx->V[i],&vdata);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SolveAddLinearSystem"
static PetscErrorCode SolveAddLinearSystem(EPS eps,PetscInt Ladd_start,PetscInt Ladd_end)
{
  PetscErrorCode ierr;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;
  PetscInt       i,j;

  PetscFunctionBegin;
  for (i=0;i<ctx->num_solve_point;i++) {
    for (j=Ladd_start;j<Ladd_end;j++) {
      ierr = VecDestroy(&ctx->Y[i*ctx->L_max+j]);CHKERRQ(ierr);
      ierr = VecDuplicate(ctx->V[0],&ctx->Y[i*ctx->L_max+j]);CHKERRQ(ierr);
      ierr = PetscLogObjectParent((PetscObject)eps,(PetscObject)ctx->Y[i*ctx->L_max+j]);CHKERRQ(ierr);
      ierr = KSPSolve(ctx->ksp[i],ctx->V[j],ctx->Y[i*ctx->L_max+j]);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "CalcMu"
static PetscErrorCode CalcMu(EPS eps,PetscScalar *Mu)
{
  PetscErrorCode ierr;
  PetscInt       i,j,k,s;
  PetscMPIInt    rank_region,icolor,ikey;
  PetscScalar    *temp,*temp2,*ppk,alp;
  MPI_Comm       Row_Comm;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;

  PetscFunctionBegin;
  ierr = MPI_Comm_rank(PetscObjectComm((PetscObject)eps),&rank_region);CHKERRQ(ierr);
  icolor = rank_region % ctx->npart;
  ikey = rank_region / ctx->npart;
  ierr = MPI_Comm_split(PetscObjectComm((PetscObject)eps),icolor,ikey,&Row_Comm);CHKERRQ(ierr);

  ierr = PetscMalloc1(ctx->num_solve_point*ctx->L*ctx->L,&temp);CHKERRQ(ierr);
  ierr = PetscMalloc1(2*ctx->M*ctx->L*ctx->L,&temp2);CHKERRQ(ierr);
  ierr = PetscMalloc1(ctx->num_solve_point,&ppk);CHKERRQ(ierr);
  for (i=0;i<2*ctx->M*ctx->L*ctx->L;i++) temp2[i] = 0;
  for (i=0; i<ctx->num_solve_point;i++) {
    for (j=0;j<ctx->L;j++) {
      ierr = VecMDot(ctx->Y[i*ctx->L_max+j],ctx->L,ctx->V,&temp[(j+i*ctx->L)*ctx->L]);CHKERRQ(ierr);
    }
  }

  for (i=0;i<ctx->num_solve_point;i++) ppk[i] = 1;
  for (k=0;k<2*ctx->M;k++) {
    for (j=0;j<ctx->L;j++) {
      for (i=0;i<ctx->num_solve_point;i++) {
  	alp = ppk[i]*ctx->weight[ctx->solver_comm_id*ctx->num_solve_point+i]/(PetscReal)ctx->N;
	for (s=0;s<ctx->L;s++) {
	  if (ctx->useconj) temp2[s+(j+k*ctx->L)*ctx->L] += PetscRealPart(alp*temp[s+(j+i*ctx->L)*ctx->L])*2;
	  else temp2[s+(j+k*ctx->L)*ctx->L] += alp*temp[s+(j+i*ctx->L)*ctx->L];
	}
      }
    }
    for (i=0;i<ctx->num_solve_point;i++) 
      ppk[i] *= ctx->pp[ctx->solver_comm_id*ctx->num_solve_point+i];
  }
  ierr = MPI_Allreduce(temp2,Mu,2*ctx->M*ctx->L*ctx->L,MPIU_SCALAR,MPIU_SUM,Row_Comm);CHKERRQ(ierr);

  ierr = PetscFree(ppk);CHKERRQ(ierr);
  ierr = PetscFree(temp);CHKERRQ(ierr);
  ierr = PetscFree(temp2);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "BlockHankel"
static PetscErrorCode BlockHankel(EPS eps,PetscScalar *Mu,PetscInt s,Vec *H)
{
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;
  PetscInt       i,j,k,L=ctx->L,M=ctx->M;
  PetscScalar    *H_data; 
  PetscErrorCode ierr;

  PetscFunctionBegin;
  for (k=0;k<L*M;k++) {
    ierr = VecGetArray(H[k],&H_data);CHKERRQ(ierr);
    for (j=0;j<M;j++) 
      for (i=0;i<L;i++) 
	H_data[j*L+i] = Mu[i+k*L+(j+s)*L*L];
    ierr = VecRestoreArray(H[k],&H_data);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SVD"
static PetscErrorCode SVD(EPS eps,Vec *Q,PetscInt *K,PetscBool isqr)
{
  PetscErrorCode ierr;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;
  PetscInt       i,j,k,ld,ml=ctx->L*ctx->M,n=eps->n;
  PetscScalar    *R,*w,*s;
  DS             ds;

  PetscFunctionBegin;
  if (isqr) {
    ierr = PetscCalloc1(ml*ml,&s);CHKERRQ(ierr);
    ierr = IPQRDecomposition(eps->ip,Q,0,ml,s,ml);CHKERRQ(ierr);
  }

  ierr = DSCreate(PETSC_COMM_WORLD,&ds);CHKERRQ(ierr);
  ierr = DSSetType(ds,DSSVD);CHKERRQ(ierr);
  ierr = DSSetFromOptions(ds);CHKERRQ(ierr);
  ld = ml;
  ierr = DSAllocate(ds,ld);CHKERRQ(ierr);
  k = PetscMin(n,ml);
  ierr = DSSetDimensions(ds,k,ml,0,0);CHKERRQ(ierr);
  ierr = DSGetArray(ds,DS_MAT_A,&R);CHKERRQ(ierr);
  if (isqr) {
    for (i=0;i<ml;i++) 
      for (j=0;j<k;j++) 
	R[i*ld+j] = s[i*ml+j];
  } else {
    for (i=0;i<ml;i++) {
      ierr = VecGetArray(Q[i],&s);CHKERRQ(ierr);
      for (j=0;j<k;j++) {
	R[i*ld+j] = s[j];
      }
      ierr = VecRestoreArray(Q[i],&s);CHKERRQ(ierr);
    }
  }
  ierr = DSRestoreArray(ds,DS_MAT_A,&R);CHKERRQ(ierr);
  if (isqr) { ierr = PetscFree(s);CHKERRQ(ierr); }
  ierr = DSSetState(ds,DS_STATE_RAW);CHKERRQ(ierr);
  ierr = PetscMalloc1(k,&w);CHKERRQ(ierr);
  ierr = DSSetEigenvalueComparison(ds,SlepcCompareLargestReal,NULL);CHKERRQ(ierr);
  ierr = DSSolve(ds,w,NULL);CHKERRQ(ierr);
  ierr = DSSort(ds,w,NULL,NULL,NULL,NULL);CHKERRQ(ierr);
  (*K) = 0;
  for (i=0;i<k;i++) {
    ctx->sigma[i] = PetscRealPart(w[i]);
    if (ctx->sigma[i]/PetscMax(ctx->sigma[0],1)>ctx->delta) (*K)++;
  }
  ierr = PetscFree(w);CHKERRQ(ierr);
  ierr = DSDestroy(&ds);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "ProjectMatrix"
static PetscErrorCode ProjectMatrix(Mat A,PetscInt nv,PetscInt ld,Vec *Q,PetscScalar *H,Vec w,PetscBool isherm)
{
  PetscErrorCode ierr;
  PetscInt       i,j;

  PetscFunctionBegin;
  if (isherm) {
    for (j=0;j<nv;j++) {
      ierr = MatMult(A,Q[j],w);CHKERRQ(ierr);
      ierr = VecMDot(w,j+1,Q,H+j*ld);CHKERRQ(ierr);
      for (i=0;i<j;i++)
        H[j+i*ld] = PetscConj(H[i+j*ld]);
    }
  } else {
    for (j=0;j<nv;j++) {
      ierr = MatMult(A,Q[j],w);CHKERRQ(ierr);
      ierr = VecMDot(w,nv,Q,H+j*ld);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "isInsideGamma"
static PetscErrorCode isInsideGamma(EPS eps,PetscInt nv,PetscBool *fl)
{
  EPS_CISS    *ctx = (EPS_CISS*)eps->data;
  PetscInt    i;
  PetscScalar d;
  PetscReal   dx,dy;
  for (i=0;i<nv;i++) {
    d = (eps->eigr[i]-ctx->center)/ctx->radius;
    dx = PetscRealPart(d);
    dy = PetscImaginaryPart(d);
    if ((dx*dx+(dy*dy)/(ctx->vscale*ctx->vscale))<=1) fl[i] = PETSC_TRUE;
    else fl[i] = PETSC_FALSE;
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSSetUp_CISS"
PetscErrorCode EPSSetUp_CISS(EPS eps)
{
  PetscErrorCode ierr;
  PetscInt       i;
  Vec            stemp;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;
  const char     *prefix;

  PetscFunctionBegin;
#if !defined(PETSC_USE_COMPLEX)
  SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_SUP,"CISS only works for complex scalars");
#endif
  eps->ncv = PetscMin(eps->n,ctx->L*ctx->M);
  if (!eps->mpd) eps->mpd = eps->ncv;
  if (!eps->which) eps->which = EPS_ALL;
  if (!eps->extraction) {
    ierr = EPSSetExtraction(eps,EPS_RITZ);CHKERRQ(ierr);
  } else if (eps->extraction!=EPS_RITZ) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_SUP,"Unsupported extraction type");
  if (eps->arbitrary) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_SUP,"Arbitrary selection of eigenpairs not supported in this solver");

  if (ctx->isreal && PetscImaginaryPart(ctx->center) == 0.0) ctx->useconj = PETSC_TRUE;
  else ctx->useconj = PETSC_FALSE;

  if (!ctx->delta) ctx->delta = PetscMin((eps->tol==PETSC_DEFAULT?SLEPC_DEFAULT_TOL*1e-1:eps->tol*1e-1),1e-12);

  if (!ctx->vscale) {
    if (eps->ishermitian && (eps->ispositive || !eps->isgeneralized) && PetscImaginaryPart(ctx->center) == 0.0) ctx->vscale = 0.1;
    else ctx->vscale = 1.0;
  }

  /* create split comm */
  ierr = SetSolverComm(eps);CHKERRQ(ierr);

  ierr = EPSAllocateSolution(eps,0);CHKERRQ(ierr);
  ierr = PetscMalloc1(ctx->N,&ctx->weight);CHKERRQ(ierr);
  ierr = PetscMalloc1(ctx->N,&ctx->omega);CHKERRQ(ierr);
  ierr = PetscMalloc1(ctx->N,&ctx->pp);CHKERRQ(ierr);
  ierr = PetscLogObjectMemory((PetscObject)eps,3*ctx->N*sizeof(PetscScalar));CHKERRQ(ierr);
  ierr = PetscMalloc1(ctx->L*ctx->M,&ctx->sigma);CHKERRQ(ierr);

  /* create a template vector for Vecs on solver communicator */
  ierr = VecCreateMPI(PetscObjectComm((PetscObject)eps),PETSC_DECIDE,eps->n,&stemp); CHKERRQ(ierr);
  ierr = VecDuplicateVecs(stemp,ctx->L,&ctx->V);CHKERRQ(ierr);
  ierr = PetscLogObjectParents(eps,ctx->L,ctx->V);CHKERRQ(ierr);
  ierr = VecDestroy(&stemp);CHKERRQ(ierr);

  ierr = PetscMalloc1(ctx->num_solve_point,&ctx->ksp);CHKERRQ(ierr);
  ierr = PetscLogObjectMemory((PetscObject)eps,ctx->num_solve_point*sizeof(KSP));CHKERRQ(ierr);
  for (i=0;i<ctx->num_solve_point;i++) {
    ierr = KSPCreate(PetscObjectComm((PetscObject)eps),&ctx->ksp[i]);CHKERRQ(ierr);
    ierr = PetscObjectIncrementTabLevel((PetscObject)ctx->ksp[i],(PetscObject)eps,1);CHKERRQ(ierr);
    ierr = PetscLogObjectParent((PetscObject)eps,(PetscObject)ctx->ksp[i]);CHKERRQ(ierr);
    ierr = KSPAppendOptionsPrefix(ctx->ksp[i],"eps_ciss_");CHKERRQ(ierr);
    ierr = EPSGetOptionsPrefix(eps,&prefix);CHKERRQ(ierr);
    ierr = KSPAppendOptionsPrefix(ctx->ksp[i],prefix);CHKERRQ(ierr);
  }
  ierr = PetscCalloc1(ctx->num_solve_point*ctx->L_max,&ctx->Y);CHKERRQ(ierr);
  ierr = PetscLogObjectMemory((PetscObject)eps,ctx->num_solve_point*ctx->L_max*sizeof(Vec));CHKERRQ(ierr);

  if (eps->isgeneralized) {
    if (eps->ishermitian && eps->ispositive) {
      ierr = DSSetType(eps->ds,DSGHEP);CHKERRQ(ierr);
    } else {
      ierr = DSSetType(eps->ds,DSGNHEP);CHKERRQ(ierr);
    }
  } else {
    if (eps->ishermitian) {
      ierr = DSSetType(eps->ds,DSHEP);CHKERRQ(ierr);
    } else {
      ierr = DSSetType(eps->ds,DSNHEP);CHKERRQ(ierr);
    }
  }
  ierr = DSAllocate(eps->ds,eps->ncv);CHKERRQ(ierr);
  ierr = EPSSetWorkVecs(eps,2);CHKERRQ(ierr);

  /* dispatch solve method */
  if (eps->leftvecs) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_SUP,"Left vectors not supported in this solver");
  eps->ops->solve = EPSSolve_CISS;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSSolve_CISS"
PetscErrorCode EPSSolve_CISS(EPS eps)
{
  PetscErrorCode ierr;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;
  PetscInt       i,j,k,ld,nv,nmat,nvecs,L_add=0,inner,outer,L_base=ctx->L;
  PetscScalar    *H,*rr,*pX,*tdata,*vdata,*Mu;
  PetscReal      *tau,s1,s2,tau_max=0.0,*temp,error,max_error;
  PetscBool      *fl;
  Mat            A,B;
  Vec            w=eps->work[0],tempv=eps->work[1],*H0,aux;

  PetscFunctionBegin;
  ierr = DSGetLeadingDimension(eps->ds,&ld);CHKERRQ(ierr);
  ierr = STGetNumMatrices(eps->st,&nmat);CHKERRQ(ierr);
  ierr = STGetOperators(eps->st,0,&A);CHKERRQ(ierr);
  if (nmat>1) { ierr = STGetOperators(eps->st,1,&B);CHKERRQ(ierr); }
  else B = NULL;

  ierr = SetPathParameter(eps);CHKERRQ(ierr);
  for (i=0;i<ctx->L;i++) {
    ierr = CISSVecSetRandom(ctx->V[i],eps->rand);CHKERRQ(ierr);
  }
  ierr = SolveLinearSystem(eps);CHKERRQ(ierr);
  ierr = ConstructS(eps,1,&ctx->S);CHKERRQ(ierr);
  nvecs = ctx->L;
  ierr = EstimateNumberEigs(eps,ctx->S,&L_add);CHKERRQ(ierr);

  if (L_add>0) {
    ierr = PetscInfo2(eps,"Changing L %d -> %d by Estimate #Eig\n",ctx->L,ctx->L+L_add);CHKERRQ(ierr);
    ierr = SetAddVector(eps,ctx->L+L_add);CHKERRQ(ierr);
    ierr = SolveAddLinearSystem(eps,ctx->L,ctx->L+L_add);CHKERRQ(ierr);
    ctx->L += L_add;
    ierr = PetscFree(ctx->sigma);CHKERRQ(ierr);
    ierr = PetscMalloc1(ctx->L*ctx->M,&ctx->sigma);CHKERRQ(ierr);
  }

  for (i=0;i<ctx->refine_blocksize;i++) {
    ierr = PetscMalloc1(ctx->L*ctx->L*ctx->M*2,&Mu);CHKERRQ(ierr);
    ierr = CalcMu(eps,Mu);CHKERRQ(ierr);
    ierr = VecCreateMPI(PetscObjectComm((PetscObject)eps),PETSC_DECIDE,ctx->L*ctx->M,&aux);CHKERRQ(ierr);
    ierr = VecDuplicateVecs(aux,ctx->L*ctx->M,&H0);CHKERRQ(ierr);
    ierr = VecDestroy(&aux);CHKERRQ(ierr);
    ierr = BlockHankel(eps,Mu,0,H0);CHKERRQ(ierr);
    ierr = SVD(eps,H0,&nv,PETSC_FALSE);CHKERRQ(ierr);
    ierr = PetscFree(Mu);CHKERRQ(ierr);
    ierr = VecDestroyVecs(ctx->L*ctx->M,&H0);CHKERRQ(ierr);
    if (ctx->sigma[0]<=ctx->delta || nv < ctx->L*ctx->M) break;
    L_add = L_base;
    ierr = PetscInfo2(eps,"Changing L %d -> %d by SVD(H0)\n",ctx->L,ctx->L+L_add);CHKERRQ(ierr);
    ierr = SetAddVector(eps,ctx->L+L_add);CHKERRQ(ierr);
    ierr = SolveAddLinearSystem(eps,ctx->L,ctx->L+L_add);CHKERRQ(ierr);
    ctx->L += L_add;
    ierr = PetscFree(ctx->sigma);CHKERRQ(ierr);
    ierr = PetscMalloc1(ctx->L*ctx->M,&ctx->sigma);CHKERRQ(ierr);
  }

  if (ctx->L != L_base) {
    eps->ncv = PetscMin(eps->n,ctx->L*ctx->M);
    eps->mpd = eps->ncv;
    ierr = EPSAllocateSolution(eps,0);CHKERRQ(ierr);
    ierr = DSReset(eps->ds);CHKERRQ(ierr);
    ierr = DSSetEigenvalueComparison(eps->ds,eps->comparison,eps->comparisonctx);CHKERRQ(ierr);
    if (eps->isgeneralized) {
      if (eps->ishermitian && eps->ispositive) {
        ierr = DSSetType(eps->ds,DSGHEP);CHKERRQ(ierr);
      } else {
        ierr = DSSetType(eps->ds,DSGNHEP);CHKERRQ(ierr);
      }
    } else {
      if (eps->ishermitian) {
        ierr = DSSetType(eps->ds,DSHEP);CHKERRQ(ierr);
      } else {
        ierr = DSSetType(eps->ds,DSNHEP);CHKERRQ(ierr);
      }
    }
    ierr = DSAllocate(eps->ds,eps->ncv);CHKERRQ(ierr);
    ierr = DSGetLeadingDimension(eps->ds,&ld);CHKERRQ(ierr);
  }

  for (outer=0;outer<=ctx->refine_outer;outer++) {
    for (inner=0;inner<=ctx->refine_inner;inner++) {
      ierr = VecDestroyVecs(nvecs,&ctx->S);CHKERRQ(ierr);
      ierr = ConstructS(eps,ctx->M,&ctx->S);CHKERRQ(ierr);
      nvecs = ctx->M*ctx->L;
      ierr = SVD(eps,ctx->S,&nv,PETSC_TRUE);CHKERRQ(ierr);
      if (ctx->sigma[0]>ctx->delta && nv==ctx->L*ctx->M && inner!=ctx->refine_inner) {
        for (i=0;i<ctx->L;i++) {
          ierr = VecCopy(ctx->S[i],ctx->V[i]);CHKERRQ(ierr);
        }
        ierr = SolveAddLinearSystem(eps,0,ctx->L);CHKERRQ(ierr);
      } else break;
    }
    eps->nconv = 0;
    if (nv == 0) break;
    ierr = DSSetDimensions(eps->ds,nv,0,0,0);CHKERRQ(ierr);
    ierr = DSSetState(eps->ds,DS_STATE_RAW);CHKERRQ(ierr);

    ierr = DSGetArray(eps->ds,DS_MAT_A,&H);CHKERRQ(ierr);
    ierr = ProjectMatrix(A,nv,ld,ctx->S,H,w,eps->ishermitian);CHKERRQ(ierr);
    ierr = DSRestoreArray(eps->ds,DS_MAT_A,&H);CHKERRQ(ierr);
    
    if (nmat>1) {
      ierr = DSGetArray(eps->ds,DS_MAT_B,&H);CHKERRQ(ierr);
      ierr = ProjectMatrix(B,nv,ld,ctx->S,H,w,eps->ishermitian);CHKERRQ(ierr);
      ierr = DSRestoreArray(eps->ds,DS_MAT_B,&H);CHKERRQ(ierr);
    }
    
    ierr = DSSolve(eps->ds,eps->eigr,NULL);CHKERRQ(ierr);

    ierr = PetscMalloc1(nv,&tau);CHKERRQ(ierr);
    ierr = DSVectors(eps->ds,DS_MAT_X,NULL,NULL);CHKERRQ(ierr);
    ierr = DSGetArray(eps->ds,DS_MAT_X,&pX);CHKERRQ(ierr);
    for (i=0;i<nv;i++) {
      s1 = 0;
      s2 = 0;
      for (j=0;j<nv;j++) {
        s1 += PetscAbsScalar(PetscPowScalar(pX[i*ld+j],2));
        s2 += PetscPowScalar(PetscAbsScalar(pX[i*ld+j]),2)/ctx->sigma[j];
      }
      tau[i] = s1/s2;
      tau_max = PetscMax(tau_max,tau[i]);
    }
    tau_max /= ctx->sigma[0];
    ierr = DSRestoreArray(eps->ds,DS_MAT_X,&pX);CHKERRQ(ierr);
    for (i=0;i<nv;i++) tau[i] /= tau_max;
    ierr = PetscMalloc1(nv,&fl);CHKERRQ(ierr);
    ierr = isInsideGamma(eps,nv,fl);CHKERRQ(ierr);
    ierr = PetscMalloc1(nv,&rr);CHKERRQ(ierr);
    for (i=0;i<nv;i++) {
      if (fl[i] && tau[i]>=ctx->spurious_threshold*tau_max) {
        rr[i] = 1.0;
        eps->nconv++;
      } else rr[i] = 0.0;
    }

    ierr = PetscFree(tau);CHKERRQ(ierr);
    ierr = PetscFree(fl);CHKERRQ(ierr);
    ierr = DSSetEigenvalueComparison(eps->ds,SlepcCompareLargestMagnitude,NULL);CHKERRQ(ierr);
    ierr = DSSort(eps->ds,eps->eigr,NULL,rr,NULL,&eps->nconv);CHKERRQ(ierr);
    ierr = DSSetEigenvalueComparison(eps->ds,eps->comparison,eps->comparisonctx);CHKERRQ(ierr);
    ierr = PetscFree(rr);CHKERRQ(ierr);
    for (i=0;i<nv;i++) {
      ierr = VecCopy(ctx->S[i],eps->V[i]);CHKERRQ(ierr);
    }
    
    ierr = DSVectors(eps->ds,DS_MAT_X,NULL,NULL);CHKERRQ(ierr);
    ierr = DSGetArray(eps->ds,DS_MAT_X,&pX);CHKERRQ(ierr);
    ierr = SlepcUpdateVectors(nv,ctx->S,0,eps->nconv,pX,ld,PETSC_FALSE);CHKERRQ(ierr);
    if (eps->ishermitian) {  /* compute eigenvectors */
      ierr = SlepcUpdateVectors(nv,eps->V,0,eps->nconv,pX,ld,PETSC_FALSE);CHKERRQ(ierr);
    }
    ierr = DSRestoreArray(eps->ds,DS_MAT_X,&pX);CHKERRQ(ierr);

    max_error = 0.0;
    for (i=0;i<eps->nconv;i++) {
      ierr = VecNormalize(eps->V[i],NULL);CHKERRQ(ierr);
      ierr = VecNormalize(ctx->S[i],NULL);CHKERRQ(ierr);
      ierr = EPSComputeRelativeError_Private(eps,eps->eigr[i],0,ctx->S[i],NULL,&error);CHKERRQ(ierr);
      max_error = PetscMax(max_error,error);
    }
    if (max_error <= eps->tol || outer == ctx->refine_outer) break;
    ierr = PetscMalloc1(ctx->L*eps->nconv,&temp);CHKERRQ(ierr);
    for (i=0;i<ctx->L*eps->nconv;i++) {
      ierr = PetscRandomGetValueReal(eps->rand,&temp[i]);CHKERRQ(ierr);
      temp[i] = 2*temp[i]-1;
    }
    
    for (k=0;k<ctx->L;k++) {      
      ierr = VecGetArray(tempv,&tdata);CHKERRQ(ierr);
      for (j=0;j<eps->nconv;j++) {
        ierr = VecGetArray(ctx->S[j],&vdata);CHKERRQ(ierr);
        for (i=0;i<eps->n;i++) {
          if (j==0) tdata[i] = vdata[i]*temp[j+eps->nconv*k];
          else tdata[i] = tdata[i]+vdata[i]*temp[j+eps->nconv*k];
        }
        ierr = VecRestoreArray(ctx->S[j],&vdata);CHKERRQ(ierr);
      }
      ierr = VecRestoreArray(tempv,&tdata);CHKERRQ(ierr);
      ierr = VecCopy(tempv,ctx->V[k]);CHKERRQ(ierr);
    }
    
    ierr = PetscFree(temp);CHKERRQ(ierr);
    ierr = SolveAddLinearSystem(eps,0,ctx->L);CHKERRQ(ierr);
  }
  eps->reason = EPS_CONVERGED_TOL;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSSetRegion_CISS"
static PetscErrorCode EPSCISSSetRegion_CISS(EPS eps,PetscScalar center,PetscReal radius,PetscReal vscale)
{
  EPS_CISS *ctx = (EPS_CISS*)eps->data;

  PetscFunctionBegin;
  ctx->center = center;
  if (radius) {
    if (radius == PETSC_DEFAULT) {
      ctx->radius = 1.0;
    } else {
      if (radius<0.0) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The radius argument must be > 0.0");
      ctx->radius = radius;
    }
  }
  if (vscale) {
    if (vscale<0.0) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The vscale argument must be > 0.0");
    ctx->vscale = vscale;
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSSetRegion"
/*@
   EPSCISSSetRegion - Sets the parameters defining the region where eigenvalues
   must be computed.

   Logically Collective on EPS

   Input Parameters:
+  eps - the eigenproblem solver context
.  center - center of the region
.  radius - radius of the region
-  vscale - vertical scale of the region

   Options Database Keys:
+  -eps_ciss_center - Sets the center
.  -eps_ciss_radius - Sets the radius
-  -eps_ciss_vscale - Sets the vertical scale

   Level: advanced

.seealso: EPSCISSGetRegion()
@*/
PetscErrorCode EPSCISSSetRegion(EPS eps,PetscScalar center,PetscReal radius,PetscReal vscale)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(eps,EPS_CLASSID,1);
  PetscValidLogicalCollectiveScalar(eps,center,2);
  PetscValidLogicalCollectiveReal(eps,radius,3);
  PetscValidLogicalCollectiveReal(eps,vscale,4);
  ierr = PetscTryMethod(eps,"EPSCISSSetRegion_C",(EPS,PetscScalar,PetscReal,PetscReal),(eps,center,radius,vscale));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSGetRegion_CISS"
static PetscErrorCode EPSCISSGetRegion_CISS(EPS eps,PetscScalar *center,PetscReal *radius,PetscReal *vscale)
{
  EPS_CISS *ctx = (EPS_CISS*)eps->data;

  PetscFunctionBegin;
  if (center) *center = ctx->center;
  if (radius) *radius = ctx->radius;
  if (vscale) *vscale = ctx->vscale;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSGetRegion"
/*@
   EPSCISSGetRegion - Gets the parameters that define the region where eigenvalues
   must be computed.

   Not Collective

   Input Parameter:
.  eps - the eigenproblem solver context

   Output Parameters:
+  center - center of the region
.  radius - radius of the region
-  vscale - vertical scale of the region

   Level: advanced

.seealso: EPSCISSSetRegion()
@*/
PetscErrorCode EPSCISSGetRegion(EPS eps,PetscScalar *center,PetscReal *radius,PetscReal *vscale)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(eps,EPS_CLASSID,1);
  ierr = PetscTryMethod(eps,"EPSCISSGetRegion_C",(EPS,PetscScalar*,PetscReal*,PetscReal*),(eps,center,radius,vscale));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSSetSizes_CISS"
static PetscErrorCode EPSCISSSetSizes_CISS(EPS eps,PetscInt ip,PetscInt bs,PetscInt ms,PetscInt npart,PetscInt bsmax,PetscBool isreal)
{
  PetscErrorCode ierr;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;

  PetscFunctionBegin;
  if (ip) {
    if (ip == PETSC_DECIDE || ip == PETSC_DEFAULT) {
      if (ctx->N!=32) { ctx->N =32; ctx->M = ctx->N/4; }
    } else {
      if (ip<1) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The ip argument must be > 0");
      if (ip%2) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The ip argument must be an even number");
      if (ctx->N!=ip) { ctx->N = ip; ctx->M = ctx->N/4; }
    }
  }
  if (bs) {
    if (bs == PETSC_DECIDE || bs == PETSC_DEFAULT) {
      ctx->L = 16;
    } else {
      if (bs<1) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The bs argument must be > 0");
      if (bs>ctx->L_max) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The bs argument must be less than or equal to the maximum number of block size");
      ctx->L = bs;
    }
  }
  if (ms) {
    if (ms == PETSC_DECIDE || ms == PETSC_DEFAULT) {
      ctx->M = ctx->N/4;
    } else {
      if (ms<1) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The ms argument must be > 0");
      if (ms>ctx->N) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The ms argument must be less than or equal to the number of integration points");
      ctx->M = ms;
    }
  }
  if (npart) {
    if (npart == PETSC_DECIDE || npart == PETSC_DEFAULT) {
      ctx->npart = 1;
    } else {
      if (npart<1) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The npart argument must be > 0");
      ctx->npart = npart;
    }
  }
  if (bsmax) {
    if (bsmax == PETSC_DECIDE || bsmax == PETSC_DEFAULT) {
      ctx->L = 256;
    } else {
      if (bsmax<1) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The bsmax argument must be > 0");
      if (bsmax<ctx->L) ctx->L_max = ctx->L;
      else ctx->L_max = bsmax;
    }
  }
  ctx->isreal = isreal;
  ierr = EPSReset(eps);CHKERRQ(ierr);   /* clean allocated arrays and force new setup */
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSSetSizes"
/*@
   EPSCISSSetSizes - Sets the values of various size parameters in the CISS solver.

   Logically Collective on EPS

   Input Parameters:
+  eps   - the eigenproblem solver context
.  ip    - number of integration points
.  bs    - block size
.  ms    - moment size
.  npart - number of partitions when splitting the communicator
.  bsmax - max block size
-  isreal - A and B are real

   Options Database Keys:
+  -eps_ciss_integration_points - Sets the number of integration points
.  -eps_ciss_blocksize - Sets the block size
.  -eps_ciss_moments - Sets the moment size
.  -eps_ciss_partitions - Sets the number of partitions
.  -eps_ciss_maxblocksize - Sets the maximum block size
-  -eps_ciss_realmats - A and B are real

   Note:
   The default number of partitions is 1. This means the internal KSP object is shared
   among all processes of the EPS communicator. Otherwise, the communicator is split
   into npart communicators, so that npart KSP solves proceed simultaneously.

   Level: advanced

.seealso: EPSCISSGetSizes()
@*/
PetscErrorCode EPSCISSSetSizes(EPS eps,PetscInt ip,PetscInt bs,PetscInt ms,PetscInt npart,PetscInt bsmax,PetscBool isreal)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(eps,EPS_CLASSID,1);
  PetscValidLogicalCollectiveInt(eps,ip,2);
  PetscValidLogicalCollectiveInt(eps,bs,3);
  PetscValidLogicalCollectiveInt(eps,ms,4);
  PetscValidLogicalCollectiveInt(eps,npart,5);
  PetscValidLogicalCollectiveInt(eps,bsmax,6);
  PetscValidLogicalCollectiveBool(eps,isreal,7);
  ierr = PetscTryMethod(eps,"EPSCISSSetSizes_C",(EPS,PetscInt,PetscInt,PetscInt,PetscInt,PetscInt,PetscBool),(eps,ip,bs,ms,npart,bsmax,isreal));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSGetSizes_CISS"
static PetscErrorCode EPSCISSGetSizes_CISS(EPS eps,PetscInt *ip,PetscInt *bs,PetscInt *ms,PetscInt *npart,PetscInt *bsmax,PetscBool *isreal)
{
  EPS_CISS *ctx = (EPS_CISS*)eps->data;

  PetscFunctionBegin;
  if (ip) *ip = ctx->N;
  if (bs) *bs = ctx->L;
  if (ms) *ms = ctx->M;
  if (npart) *npart = ctx->npart;
  if (bsmax) *bsmax = ctx->L_max;
  if (isreal) *isreal = ctx->isreal;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSGetSizes"
/*@
   EPSCISSGetSizes - Gets the values of various size parameters in the CISS solver.

   Not Collective

   Input Parameter:
.  eps - the eigenproblem solver context

   Output Parameters:
+  ip    - number of integration points
.  bs    - block size
.  ms    - moment size
.  npart - number of partitions when splitting the communicator
.  bsmax - max block size
-  isreal - A and B are real

   Level: advanced

.seealso: EPSCISSSetSizes()
@*/
PetscErrorCode EPSCISSGetSizes(EPS eps,PetscInt *ip,PetscInt *bs,PetscInt *ms,PetscInt *npart,PetscInt *bsmax,PetscBool *isreal)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(eps,EPS_CLASSID,1);
  ierr = PetscTryMethod(eps,"EPSCISSGetSizes_C",(EPS,PetscInt*,PetscInt*,PetscInt*,PetscInt*,PetscInt*,PetscBool*),(eps,ip,bs,ms,npart,bsmax,isreal));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSSetThreshold_CISS"
static PetscErrorCode EPSCISSSetThreshold_CISS(EPS eps,PetscReal delta,PetscReal spur)
{
  EPS_CISS *ctx = (EPS_CISS*)eps->data;

  PetscFunctionBegin;
  if (delta) {
    if (delta == PETSC_DEFAULT) {
      ctx->delta = 1e-12;
    } else {
      if (delta<=0.0) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The delta argument must be > 0.0");
      ctx->delta = delta;
    }
  }
  if (spur) {
    if (spur == PETSC_DEFAULT) {
      ctx->spurious_threshold = 1e-4;
    } else {
      if (spur<=0.0) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The spurious threshold argument must be > 0.0");
      ctx->spurious_threshold = spur;
    }
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSSetThreshold"
/*@
   EPSCISSSetThreshold - Sets the values of various threshold parameters in
   the CISS solver.

   Logically Collective on EPS

   Input Parameters:
+  eps   - the eigenproblem solver context
.  delta - threshold for numerical rank
-  spur  - spurious threshold (to discard spurious eigenpairs)

   Options Database Keys:
+  -eps_ciss_delta - Sets the delta
-  -eps_ciss_spurious_threshold - Sets the spurious threshold

   Level: advanced

.seealso: EPSCISSGetThreshold()
@*/
PetscErrorCode EPSCISSSetThreshold(EPS eps,PetscReal delta,PetscReal spur)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(eps,EPS_CLASSID,1);
  PetscValidLogicalCollectiveReal(eps,delta,2);
  PetscValidLogicalCollectiveReal(eps,spur,3);
  ierr = PetscTryMethod(eps,"EPSCISSSetThreshold_C",(EPS,PetscReal,PetscReal),(eps,delta,spur));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSGetThreshold_CISS"
static PetscErrorCode EPSCISSGetThreshold_CISS(EPS eps,PetscReal *delta,PetscReal *spur)
{
  EPS_CISS *ctx = (EPS_CISS*)eps->data;

  PetscFunctionBegin;
  if (delta) *delta = ctx->delta;
  if (spur)  *spur = ctx->spurious_threshold;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSGetThreshold"
/*@
   EPSCISSGetThreshold - Gets the values of various threshold parameters
   in the CISS solver.

   Not Collective

   Input Parameter:
.  eps - the eigenproblem solver context

   Output Parameters:
+  delta - threshold for numerical rank
-  spur  - spurious threshold (to discard spurious eigenpairs)

   Level: advanced

.seealso: EPSCISSSetThreshold()
@*/
PetscErrorCode EPSCISSGetThreshold(EPS eps,PetscReal *delta,PetscReal *spur)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(eps,EPS_CLASSID,1);
  ierr = PetscTryMethod(eps,"EPSCISSGetThreshold_C",(EPS,PetscReal*,PetscReal*),(eps,delta,spur));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSSetRefinement_CISS"
static PetscErrorCode EPSCISSSetRefinement_CISS(EPS eps,PetscInt inner,PetscInt outer,PetscInt blsize)
{
  EPS_CISS *ctx = (EPS_CISS*)eps->data;

  PetscFunctionBegin;
  if (inner == PETSC_DEFAULT) {
    ctx->refine_inner = 0;
  } else {
    if (inner<0) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The refine inner argument must be >= 0");
    ctx->refine_inner = inner;
  }
  if (outer == PETSC_DEFAULT) {
    ctx->refine_outer = 0;
  } else {
    if (outer<0) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The refine outer argument must be >= 0");
    ctx->refine_outer = outer;
  }
  if (blsize == PETSC_DEFAULT) {
    ctx->refine_blocksize = 0;
  } else {
    if (blsize<0) SETERRQ(PetscObjectComm((PetscObject)eps),PETSC_ERR_ARG_OUTOFRANGE,"The refine blocksize argument must be >= 0");
    ctx->refine_blocksize = blsize;
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSSetRefinement"
/*@
   EPSCISSSetRefinement - Sets the values of various refinement parameters
   in the CISS solver.

   Logically Collective on EPS

   Input Parameters:
+  eps    - the eigenproblem solver context
.  inner  - number of iterative refinement iterations (inner loop)
.  outer  - number of iterative refinement iterations (outer loop)
-  blsize - number of iterative refinement iterations (blocksize loop)

   Options Database Keys:
+  -eps_ciss_refine_inner - Sets number of inner iterations
.  -eps_ciss_refine_outer - Sets number of outer iterations
-  -eps_ciss_refine_blocksize - Sets number of blocksize iterations

   Level: advanced

.seealso: EPSCISSGetRefinement()
@*/
PetscErrorCode EPSCISSSetRefinement(EPS eps,PetscInt inner,PetscInt outer,PetscInt blsize)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(eps,EPS_CLASSID,1);
  PetscValidLogicalCollectiveInt(eps,inner,2);
  PetscValidLogicalCollectiveInt(eps,outer,3);
  PetscValidLogicalCollectiveInt(eps,blsize,4);
  ierr = PetscTryMethod(eps,"EPSCISSSetRefinement_C",(EPS,PetscInt,PetscInt,PetscInt),(eps,inner,outer,blsize));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSGetRefinement_CISS"
static PetscErrorCode EPSCISSGetRefinement_CISS(EPS eps,PetscInt *inner,PetscInt *outer,PetscInt *blsize)
{
  EPS_CISS *ctx = (EPS_CISS*)eps->data;

  PetscFunctionBegin;
  if (inner)  *inner = ctx->refine_inner;
  if (outer)  *outer = ctx->refine_outer;
  if (blsize) *blsize = ctx->refine_blocksize;
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCISSGetRefinement"
/*@
   EPSCISSGetRefinement - Gets the values of various refinement parameters
   in the CISS solver.

   Not Collective

   Input Parameter:
.  eps - the eigenproblem solver context

   Output Parameters:
+  inner  - number of iterative refinement iterations (inner loop)
.  outer  - number of iterative refinement iterations (outer loop)
-  blsize - number of iterative refinement iterations (blocksize loop)

   Level: advanced

.seealso: EPSCISSSetRefinement()
@*/
PetscErrorCode EPSCISSGetRefinement(EPS eps, PetscInt *inner, PetscInt *outer,PetscInt *blsize)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(eps,EPS_CLASSID,1);
  ierr = PetscTryMethod(eps,"EPSCISSGetRefinement_C",(EPS,PetscInt*,PetscInt*,PetscInt*),(eps,inner,outer,blsize));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSReset_CISS"
PetscErrorCode EPSReset_CISS(EPS eps)
{
  PetscErrorCode ierr;
  PetscInt       i;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;

  PetscFunctionBegin;
  ierr = PetscFree(ctx->weight);CHKERRQ(ierr);
  ierr = PetscFree(ctx->omega);CHKERRQ(ierr);
  ierr = PetscFree(ctx->pp);CHKERRQ(ierr);
  ierr = VecDestroyVecs(ctx->L,&ctx->V);CHKERRQ(ierr);
  for (i=0;i<ctx->num_solve_point;i++) {
    ierr = KSPDestroy(&ctx->ksp[i]);CHKERRQ(ierr);
  }
  ierr = PetscFree(ctx->ksp);CHKERRQ(ierr);
  ierr = PetscFree(ctx->sigma);CHKERRQ(ierr);
  for (i=0;i<ctx->num_solve_point*ctx->L_max;i++) {
    ierr = VecDestroy(&ctx->Y[i]);CHKERRQ(ierr);
  }
  ierr = PetscFree(ctx->Y);CHKERRQ(ierr);
  ierr = VecDestroyVecs(ctx->M*ctx->L,&ctx->S);CHKERRQ(ierr);
  ierr = EPSReset_Default(eps);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSSetFromOptions_CISS"
PetscErrorCode EPSSetFromOptions_CISS(EPS eps)
{
  PetscErrorCode ierr;
  PetscScalar    s;
  PetscReal      r1,r2,r3,r4;
  PetscInt       i1=0,i2=0,i3=0,i4=0,i5=0,i6=0,i7=0,i8=0;
  PetscBool      b1=PETSC_FALSE;

  PetscFunctionBegin;
  ierr = PetscOptionsHead("EPS CISS Options");CHKERRQ(ierr);
  ierr = EPSCISSGetRegion(eps,&s,&r1,&r2);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-eps_ciss_radius","CISS radius of region","EPSCISSSetRegion",r1,&r1,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsScalar("-eps_ciss_center","CISS center of region","EPSCISSSetRegion",s,&s,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-eps_ciss_vscale","CISS vertical scale of region","EPSCISSSetRegion",r2,&r2,NULL);CHKERRQ(ierr);
  ierr = EPSCISSSetRegion(eps,s,r1,r2);CHKERRQ(ierr);

  ierr = PetscOptionsInt("-eps_ciss_integration_points","CISS number of integration points","EPSCISSSetSizes",i1,&i1,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-eps_ciss_blocksize","CISS block size","EPSCISSSetSizes",i2,&i2,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-eps_ciss_moments","CISS moment size","EPSCISSSetSizes",i3,&i3,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-eps_ciss_partitions","CISS number of partitions","EPSCISSSetSizes",i4,&i4,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-eps_ciss_maxblocksize","CISS maximum block size","EPSCISSSetSizes",i5,&i5,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsBool("-eps_ciss_realmats","CISS A and B are real","EPSCISSSetSizes",b1,&b1,NULL);CHKERRQ(ierr);
  ierr = EPSCISSSetSizes(eps,i1,i2,i3,i4,i5,b1);CHKERRQ(ierr);

  ierr = EPSCISSGetThreshold(eps,&r3,&r4);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-eps_ciss_delta","CISS threshold for numerical rank","EPSCISSSetThreshold",r3,&r3,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsReal("-eps_ciss_spurious_threshold","CISS threshold for the spurious eigenpairs","EPSCISSSetThreshold",r4,&r4,NULL);CHKERRQ(ierr);
  ierr = EPSCISSSetThreshold(eps,r3,r4);CHKERRQ(ierr);

  ierr = EPSCISSGetRefinement(eps,&i6,&i7,&i8);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-eps_ciss_refine_inner","CISS number of inner iterative refinement iterations","EPSCISSSetRefinement",i6,&i6,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-eps_ciss_refine_outer","CISS number of outer iterative refinement iterations","EPSCISSSetRefinement",i7,&i7,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsInt("-eps_ciss_refine_blocksize","CISS number of blocksize iterative refinement iterations","EPSCISSSetRefinement",i8,&i8,NULL);CHKERRQ(ierr);
  ierr = EPSCISSSetRefinement(eps,i6,i7,i8);CHKERRQ(ierr);

  ierr = PetscOptionsTail();CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSDestroy_CISS"
PetscErrorCode EPSDestroy_CISS(EPS eps)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscFree(eps->data);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSSetRegion_C",NULL);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSGetRegion_C",NULL);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSSetSizes_C",NULL);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSGetSizes_C",NULL);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSSetThreshold_C",NULL);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSGetThreshold_C",NULL);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSSetRefinement_C",NULL);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSGetRefinement_C",NULL);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSView_CISS"
PetscErrorCode EPSView_CISS(EPS eps,PetscViewer viewer)
{
  PetscErrorCode ierr;
  EPS_CISS       *ctx = (EPS_CISS*)eps->data;
  PetscBool      isascii;
  char           str[50];

  PetscFunctionBegin;
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&isascii);CHKERRQ(ierr);
  if (isascii) {
    ierr = SlepcSNPrintfScalar(str,50,ctx->center,PETSC_FALSE);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  CISS: region { center: %s, radius: %G, vscale: %G }\n",str,ctx->radius,ctx->vscale);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  CISS: sizes { integration points: %D, block size: %D, moment size: %D, partitions: %D, maximum block size: %D }\n",ctx->N,ctx->L,ctx->M,ctx->npart,ctx->L_max);CHKERRQ(ierr);
    if (ctx->isreal) {
      ierr = PetscViewerASCIIPrintf(viewer,"  CISS: exploiting symmetry of integration points\n");CHKERRQ(ierr);
    }
    ierr = PetscViewerASCIIPrintf(viewer,"  CISS: threshold { delta: %G, spurious threshold: %G }\n",ctx->delta,ctx->spurious_threshold);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  CISS: iterative refinement  { inner: %D, outer: %D, blocksize: %D }\n",ctx->refine_inner,ctx->refine_outer, ctx->refine_blocksize);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPushTab(viewer);CHKERRQ(ierr);
    ierr = KSPView(ctx->ksp[0],viewer);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPopTab(viewer);CHKERRQ(ierr);

  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "EPSCreate_CISS"
PETSC_EXTERN PetscErrorCode EPSCreate_CISS(EPS eps)
{
  PetscErrorCode ierr;
  EPS_CISS       *ctx;

  PetscFunctionBegin;
  ierr = PetscNewLog(eps,&ctx);CHKERRQ(ierr);
  eps->data = (void*)ctx;

  eps->ops->setup          = EPSSetUp_CISS;
  eps->ops->setfromoptions = EPSSetFromOptions_CISS;
  eps->ops->destroy        = EPSDestroy_CISS;
  eps->ops->reset          = EPSReset_CISS;
  eps->ops->view           = EPSView_CISS;
  eps->ops->backtransform  = PETSC_NULL;
  eps->ops->computevectors = EPSComputeVectors_Schur;
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSSetRegion_C",EPSCISSSetRegion_CISS);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSGetRegion_C",EPSCISSGetRegion_CISS);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSSetSizes_C",EPSCISSSetSizes_CISS);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSGetSizes_C",EPSCISSGetSizes_CISS);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSSetThreshold_C",EPSCISSSetThreshold_CISS);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSGetThreshold_C",EPSCISSGetThreshold_CISS);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSSetRefinement_C",EPSCISSSetRefinement_CISS);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)eps,"EPSCISSGetRefinement_C",EPSCISSGetRefinement_CISS);CHKERRQ(ierr);
  /* set default values of parameters */
  ctx->center  = 0.0;
  ctx->radius  = 1.0;
  ctx->vscale  = 0.0;
  ctx->N       = 32;
  ctx->L       = 16;
  ctx->M       = ctx->N/4;
  ctx->delta   = 0;
  ctx->npart   = 1;
  ctx->L_max   = 128;
  ctx->spurious_threshold = 1e-4;
  ctx->isreal  = PETSC_FALSE;
  ctx->refine_outer = 1;
  ctx->refine_inner = 1;
  ctx->refine_blocksize = 1;
  PetscFunctionReturn(0);
}

