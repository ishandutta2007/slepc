/*
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   SLEPc - Scalable Library for Eigenvalue Problem Computations
   Copyright (c) 2002-2017, Universitat Politecnica de Valencia, Spain

   This file is part of SLEPc.
   SLEPc is distributed under a 2-clause BSD license (see LICENSE).
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
*/
/*
   BV implemented as a single Vec (CUDA version)
*/

#include <slepc/private/bvimpl.h>
#include "../svecimpl.h"

#if defined(PETSC_USE_COMPLEX)
#include <thrust/device_ptr.h>
#endif

#define BLOCKSIZE 64

/*
    B := alpha*A + beta*B

    A,B are nxk (ld=n)
 */
static PetscErrorCode BVAXPY_BLAS_CUDA(BV bv,PetscInt n_,PetscInt k_,PetscScalar alpha,const PetscScalar *d_A,PetscScalar beta,PetscScalar *d_B)
{
  PetscErrorCode ierr;
  PetscBLASInt   m,one=1;
  cublasStatus_t cberr;

  PetscFunctionBegin;
  ierr = PetscBLASIntCast(n_*k_,&m);CHKERRQ(ierr);
  if (beta!=(PetscScalar)1.0) {
    cberr = cublasXscal(cublasv2handle,m,&beta,d_B,one);CHKERRCUBLAS(cberr);
    ierr = PetscLogFlops(m);CHKERRQ(ierr);
  }
  cberr = cublasXaxpy(cublasv2handle,m,&alpha,d_A,one,d_B,one);CHKERRCUBLAS(cberr);
  ierr = WaitForGPU();CHKERRCUDA(ierr);
  ierr = PetscLogFlops(2.0*m);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
    C := alpha*A*B + beta*C
*/
PetscErrorCode BVMult_Svec_CUDA(BV Y,PetscScalar alpha,PetscScalar beta,BV X,Mat Q)
{
  PetscErrorCode    ierr;
  BV_SVEC           *y = (BV_SVEC*)Y->data,*x = (BV_SVEC*)X->data;
  const PetscScalar *d_px,*d_A;
  PetscScalar       *d_py,*q,*d_q,*d_B,*d_C;
  PetscInt          m,n,k,ldq,mq;
  cublasStatus_t    cberr;
  cudaError_t       err;

  PetscFunctionBegin;
  m = Y->n;
  n = Y->k-Y->l;
  k = X->k-X->l;
  if (!Y->n) PetscFunctionReturn(0);
  ierr = VecCUDAGetArrayRead(x->v,&d_px);CHKERRQ(ierr);
  if (beta==(PetscScalar)0.0) {
    ierr = VecCUDAGetArrayWrite(y->v,&d_py);CHKERRQ(ierr);
  } else {
    ierr = VecCUDAGetArrayReadWrite(y->v,&d_py);CHKERRQ(ierr);
  }
  d_A = d_px+(X->nc+X->l)*X->n;
  d_C = d_py+(Y->nc+Y->l)*Y->n;
  if (Q) {
    ierr = MatGetSize(Q,&ldq,&mq);CHKERRQ(ierr);
    ierr = MatDenseGetArray(Q,&q);CHKERRQ(ierr);
    err = cudaMalloc((void**)&d_q,ldq*mq*sizeof(PetscScalar));CHKERRCUDA(err);
    err = cudaMemcpy(d_q,q,ldq*mq*sizeof(PetscScalar),cudaMemcpyHostToDevice);CHKERRCUDA(err);
    d_B = d_q+Y->l*ldq+X->l;
    cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_N,CUBLAS_OP_N,m,n,k,&alpha,d_A,m,d_B,ldq,&beta,d_C,m);CHKERRCUBLAS(cberr);
    ierr = WaitForGPU();CHKERRCUDA(ierr);
    ierr = MatDenseRestoreArray(Q,&q);CHKERRQ(ierr);
    err = cudaFree(d_q);CHKERRCUDA(err);
    ierr = PetscLogFlops(2.0*m*n*k);CHKERRQ(ierr);
  } else {
    ierr = BVAXPY_BLAS_CUDA(Y,m,n,alpha,d_A,beta,d_C);CHKERRQ(ierr);
  }
  ierr = VecCUDARestoreArrayRead(x->v,&d_px);CHKERRQ(ierr);
  ierr = VecCUDARestoreArrayWrite(y->v,&d_py);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
    y := alpha*A*x + beta*y
*/
PetscErrorCode BVMultVec_Svec_CUDA(BV X,PetscScalar alpha,PetscScalar beta,Vec y,PetscScalar *q)
{
  PetscErrorCode    ierr;
  BV_SVEC           *x = (BV_SVEC*)X->data;
  const PetscScalar *d_px,*d_A;
  PetscScalar       *d_py,*d_q,*d_x,*d_y;
  PetscBLASInt      n,k,one=1;
  cublasStatus_t    cberr;

  PetscFunctionBegin;
  n = X->n;
  k = X->k-X->l;
  ierr = VecCUDAGetArrayRead(x->v,&d_px);CHKERRQ(ierr);
  if (beta==(PetscScalar)0.0) {
    ierr = VecCUDAGetArrayWrite(y,&d_py);CHKERRQ(ierr);
  } else {
    ierr = VecCUDAGetArrayReadWrite(y,&d_py);CHKERRQ(ierr);
  }
  if (!q) {
    ierr = VecCUDAGetArrayReadWrite(X->buffer,&d_q);CHKERRQ(ierr);
  } else {
    ierr = cudaMalloc((void**)&d_q,k*sizeof(PetscScalar));CHKERRQ(ierr);
    ierr = cudaMemcpy(d_q,q,k*sizeof(PetscScalar),cudaMemcpyHostToDevice);CHKERRQ(ierr);
  }
  d_A = d_px+(X->nc+X->l)*X->n;
  d_x = d_q;
  d_y = d_py;
  cberr = cublasXgemv(cublasv2handle,CUBLAS_OP_N,n,k,&alpha,d_A,n,d_x,one,&beta,d_y,one);CHKERRCUBLAS(cberr);
  ierr = WaitForGPU();CHKERRCUDA(ierr);
  ierr = VecCUDARestoreArrayRead(x->v,&d_px);CHKERRQ(ierr);
  if (beta==(PetscScalar)0.0) {
    ierr = VecCUDARestoreArrayWrite(y,&d_py);CHKERRQ(ierr);
  } else {
    ierr = VecCUDARestoreArrayReadWrite(y,&d_py);CHKERRQ(ierr);
  }
  if (!q) {
    ierr = VecCUDARestoreArrayReadWrite(X->buffer,&d_q);CHKERRQ(ierr);
  } else {
    ierr = cudaFree(d_q);CHKERRQ(ierr);
  }
  ierr = PetscLogFlops(2.0*n*k);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
    A(:,s:e-1) := A*B(:,s:e-1)
*/
PetscErrorCode BVMultInPlace_Svec_CUDA(BV V,Mat Q,PetscInt s,PetscInt e)
{
  PetscErrorCode ierr;
  BV_SVEC        *ctx = (BV_SVEC*)V->data;
  PetscScalar    *d_pv,*q,*d_q,*d_A,*d_B,*d_work,sone=1.0,szero=0.0;
  PetscInt       m,n,j,k,l,ldq,nq,bs=BLOCKSIZE;
  cublasStatus_t cberr;
  size_t         freemem,totmem;

  PetscFunctionBegin;
  m = V->n;
  n = e-s;
  k = V->k-V->l;
  if (!m) PetscFunctionReturn(0);
  ierr = MatGetSize(Q,&ldq,&nq);CHKERRQ(ierr);
  ierr = VecCUDAGetArrayReadWrite(ctx->v,&d_pv);CHKERRQ(ierr);
  ierr = MatDenseGetArray(Q,&q);CHKERRQ(ierr);
  ierr = cudaMalloc((void**)&d_q,ldq*nq*sizeof(PetscScalar));CHKERRQ(ierr);
  ierr = cudaMemcpy(d_q,q,ldq*nq*sizeof(PetscScalar),cudaMemcpyHostToDevice);CHKERRQ(ierr);
  /* try to allocate the whole matrix */
  ierr = cudaMemGetInfo(&freemem,&totmem);CHKERRQ(ierr);
  if (freemem>=m*n*sizeof(PetscScalar)) {
    ierr = cudaMalloc((void**)&d_work,m*n*sizeof(PetscScalar));
    d_A = d_pv+(V->nc+V->l)*m;
    d_B = d_q+V->l*ldq+V->l+(s-V->l)*ldq;
    cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_N,CUBLAS_OP_N,m,n,k,&sone,d_A,m,d_B,ldq,&szero,d_work,m);CHKERRCUBLAS(cberr);
    for (j=0;j<n;j++) {
      ierr = cudaMemcpy(d_A+(s-V->l+j)*m,d_work+(j*m),m*sizeof(PetscScalar),cudaMemcpyDeviceToDevice);CHKERRCUDA(ierr);
    }
  } else {
    bs = freemem/(m*sizeof(PetscScalar));
    ierr = cudaMalloc((void**)&d_work,bs*n*sizeof(PetscScalar));CHKERRQ(ierr);
    l = m % bs;
    if (l) {
      d_A = d_pv+(V->nc+V->l)*m;
      d_B = d_q+V->l*ldq+V->l+(s-V->l)*ldq;
      cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_N,CUBLAS_OP_N,l,n,k,&sone,d_A,m,d_B,ldq,&szero,d_work,l);CHKERRCUBLAS(cberr);
      for (j=0;j<n;j++) {
        ierr = cudaMemcpy(d_A+(s-V->l+j)*m,d_work+(j*l),l*sizeof(PetscScalar),cudaMemcpyDeviceToDevice);CHKERRCUDA(ierr);
      }
    }
    for (;l<m;l+=bs) {
      d_A = d_pv+(V->nc+V->l)*m+l;
      d_B = d_q+V->l*ldq+V->l+(s-V->l)*ldq;
      cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_N,CUBLAS_OP_N,bs,n,k,&sone,d_A,m,d_B,ldq,&szero,d_work,bs);CHKERRCUBLAS(cberr);
      for (j=0;j<n;j++) {
        ierr = cudaMemcpy(d_A+(s-V->l+j)*m,d_work+(j*bs),bs*sizeof(PetscScalar),cudaMemcpyDeviceToDevice);CHKERRCUDA(ierr);
      }
    }
  }

  ierr = WaitForGPU();CHKERRCUDA(ierr);
  ierr = MatDenseRestoreArray(Q,&q);CHKERRQ(ierr);
  ierr = cudaFree(d_q);CHKERRQ(ierr);
  ierr = cudaFree(d_work);CHKERRQ(ierr);
  ierr = VecCUDARestoreArrayReadWrite(ctx->v,&d_pv);CHKERRQ(ierr);
  ierr = PetscLogFlops(2.0*m*n*k);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
    A(:,s:e-1) := A*B(:,s:e-1)
*/
PetscErrorCode BVMultInPlaceTranspose_Svec_CUDA(BV V,Mat Q,PetscInt s,PetscInt e)
{
  PetscErrorCode ierr;
  BV_SVEC        *ctx = (BV_SVEC*)V->data;
  PetscScalar    *d_pv,*q,*d_q,*d_A,*d_B,*d_work,sone=1.0,szero=0.0;
  PetscInt       m,n,j,k,ldq,nq;
  cublasStatus_t cberr;

  PetscFunctionBegin;
  m = V->n;
  n = e-s;
  k = V->k-V->l;
  if (!m) PetscFunctionReturn(0);
  ierr = MatGetSize(Q,&ldq,&nq);CHKERRQ(ierr);
  ierr = VecCUDAGetArrayReadWrite(ctx->v,&d_pv);CHKERRQ(ierr);
  ierr = MatDenseGetArray(Q,&q);CHKERRQ(ierr);
  ierr = cudaMalloc((void**)&d_q,ldq*nq*sizeof(PetscScalar));CHKERRQ(ierr);
  ierr = cudaMemcpy(d_q,q,ldq*nq*sizeof(PetscScalar),cudaMemcpyHostToDevice);CHKERRQ(ierr);
  ierr = cudaMalloc((void**)&d_work,m*n*sizeof(PetscScalar));CHKERRQ(ierr);
  d_A = d_pv+(V->nc+V->l)*m;
  d_B = d_q+V->l*ldq+s;
  cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_N,CUBLAS_OP_C,m,n,k,&sone,d_A,m,d_B,ldq,&szero,d_work,m);CHKERRCUBLAS(cberr);
  for (j=0;j<n;j++) {
    ierr = cudaMemcpy(d_A+(s-V->l+j)*m,d_work+(j*m),m*sizeof(PetscScalar),cudaMemcpyDeviceToDevice);CHKERRQ(ierr);
  }
  ierr = WaitForGPU();CHKERRCUDA(ierr);
  ierr = MatDenseRestoreArray(Q,&q);CHKERRQ(ierr);
  ierr = cudaFree(d_q);CHKERRQ(ierr);
  ierr = cudaFree(d_work);CHKERRQ(ierr);
  ierr = VecCUDARestoreArrayReadWrite(ctx->v,&d_pv);CHKERRQ(ierr);
  ierr = PetscLogFlops(2.0*m*n*k);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
    C := A'*B
*/
PetscErrorCode BVDot_Svec_CUDA(BV X,BV Y,Mat M)
{
  PetscErrorCode    ierr;
  BV_SVEC           *x = (BV_SVEC*)X->data,*y = (BV_SVEC*)Y->data;
  const PetscScalar *d_px,*d_py,*d_A,*d_B;
  PetscScalar       *pm,*d_work,sone=1.0,szero=0.0,*C,*CC;
  PetscInt          ldm,m,n,k,len,j;
  cublasStatus_t    cberr;

  PetscFunctionBegin;
  m = Y->k-Y->l;
  n = X->k-X->l;
  k = X->n;
  ierr = MatGetSize(M,&ldm,NULL);CHKERRQ(ierr);
  ierr = VecCUDAGetArrayRead(x->v,&d_px);CHKERRQ(ierr);
  ierr = VecCUDAGetArrayRead(y->v,&d_py);CHKERRQ(ierr);
  ierr = MatDenseGetArray(M,&pm);CHKERRQ(ierr);
  ierr = cudaMalloc((void**)&d_work,m*n*sizeof(PetscScalar));CHKERRQ(ierr);
  d_A = d_py+(Y->nc+Y->l)*Y->n;
  d_B = d_px+(X->nc+X->l)*X->n;
  C = pm+X->l*ldm+Y->l;
  if (x->mpi) {
    if (ldm==m) {
      ierr = BVAllocateWork_Private(X,m*n);CHKERRQ(ierr);
      if (k) {
        cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_C,CUBLAS_OP_N,m,n,k,&sone,d_A,k,d_B,k,&szero,d_work,ldm);CHKERRCUBLAS(cberr);
        ierr = cudaMemcpy(X->work,d_work,m*n*sizeof(PetscScalar),cudaMemcpyDeviceToHost);CHKERRQ(ierr);
      } else {
        ierr = PetscMemzero(X->work,m*n*sizeof(PetscScalar));CHKERRQ(ierr);
      }
      ierr = PetscMPIIntCast(m*n,&len);CHKERRQ(ierr);
      ierr = MPI_Allreduce(X->work,C,len,MPIU_SCALAR,MPIU_SUM,PetscObjectComm((PetscObject)X));CHKERRQ(ierr);
    } else {
      ierr = BVAllocateWork_Private(X,2*m*n);CHKERRQ(ierr);
      CC = X->work+m*n;
      if (k) {
        cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_C,CUBLAS_OP_N,m,n,k,&sone,d_A,k,d_B,k,&szero,d_work,m);CHKERRCUBLAS(cberr);
        ierr = cudaMemcpy(X->work,d_work,m*n*sizeof(PetscScalar),cudaMemcpyDeviceToHost);CHKERRQ(ierr);
      } else {
        ierr = PetscMemzero(X->work,m*n*sizeof(PetscScalar));CHKERRQ(ierr);
      }
      ierr = PetscMPIIntCast(m*n,&len);CHKERRQ(ierr);
      ierr = MPI_Allreduce(X->work,CC,len,MPIU_SCALAR,MPIU_SUM,PetscObjectComm((PetscObject)X));CHKERRQ(ierr);
      for (j=0;j<n;j++) {
        ierr = PetscMemcpy(C+j*ldm,CC+j*m,m*sizeof(PetscScalar));CHKERRQ(ierr);
      }
    }
  } else {
    if (k) {
      ierr = BVAllocateWork_Private(X,m*n);CHKERRQ(ierr);
      cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_C,CUBLAS_OP_N,m,n,k,&sone,d_A,k,d_B,k,&szero,d_work,m);CHKERRCUBLAS(cberr);
      ierr = cudaMemcpy(X->work,d_work,m*n*sizeof(PetscScalar),cudaMemcpyDeviceToHost);CHKERRQ(ierr);
      for (j=0;j<n;j++) {
        ierr = PetscMemcpy(C+j*ldm,X->work+j*m,m*sizeof(PetscScalar));CHKERRQ(ierr);
      }
    }
  }
  ierr = WaitForGPU();CHKERRCUDA(ierr);
  ierr = cudaFree(d_work);CHKERRQ(ierr);
  ierr = MatDenseRestoreArray(M,&pm);CHKERRQ(ierr);
  ierr = VecCUDARestoreArrayRead(x->v,&d_px);CHKERRQ(ierr);
  ierr = VecCUDARestoreArrayRead(y->v,&d_py);CHKERRQ(ierr);
  ierr = PetscLogFlops(2.0*m*n*k);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#if defined(PETSC_USE_COMPLEX)
struct conjugate
{
  __host__ __device__
    PetscScalar operator()(PetscScalar x)
    {
      return PetscConj(x);
    }
};

PetscErrorCode ConjugateCudaArray(PetscScalar *a, PetscInt n)
{
  PetscErrorCode                  ierr;
  thrust::device_ptr<PetscScalar> ptr;

  PetscFunctionBegin;
  try {
    ptr = thrust::device_pointer_cast(a);
    thrust::transform(ptr,ptr+n,ptr,conjugate());
    ierr = WaitForGPU();CHKERRCUDA(ierr);
  } catch (char *ex) {
    SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_LIB,"Thrust error: %s", ex);
  }
  PetscFunctionReturn(0);
}
#endif

/*
    y := A'*x computed as y' := x'*A
*/
PetscErrorCode BVDotVec_Svec_CUDA(BV X,Vec y,PetscScalar *q)
{
  PetscErrorCode    ierr;
  BV_SVEC           *x = (BV_SVEC*)X->data;
  const PetscScalar *d_A,*d_x,*d_px,*d_py;
  PetscScalar       *d_work,szero=0.0,sone=1.0,*qq=q;
  PetscBLASInt      n,k,one=1,len;
  Vec               z = y;
  cublasStatus_t    cberr;

  PetscFunctionBegin;
  n = X->n;
  k = X->k-X->l;
  if (X->matrix) {
    ierr = BV_IPMatMult(X,y);CHKERRQ(ierr);
    z = X->Bx;
  }
  ierr = VecCUDAGetArrayRead(x->v,&d_px);CHKERRQ(ierr);
  ierr = VecCUDAGetArrayRead(z,&d_py);CHKERRQ(ierr);
  if (!q) {
    ierr = VecCUDAGetArrayWrite(X->buffer,&d_work);CHKERRQ(ierr);
  } else {
    ierr = cudaMalloc((void**)&d_work,k*sizeof(PetscScalar));CHKERRQ(ierr);
  }
  d_A = d_px+(X->nc+X->l)*X->n;
  d_x = d_py;
  if (x->mpi) {
    ierr = BVAllocateWork_Private(X,k);CHKERRQ(ierr);
    if (n) {
#if defined(PETSC_USE_COMPLEX)
      cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_C,CUBLAS_OP_N,one,k,n,&sone,d_x,n,d_A,n,&szero,d_work,one);CHKERRCUBLAS(cberr);
      ierr = ConjugateCudaArray(d_work,k);CHKERRQ(ierr);
#else
      cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_N,CUBLAS_OP_N,one,k,n,&sone,d_x,one,d_A,n,&szero,d_work,one);CHKERRCUBLAS(cberr);
#endif
      ierr = cudaMemcpy(X->work,d_work,k*sizeof(PetscScalar),cudaMemcpyDeviceToHost);CHKERRQ(ierr);
    } else {
      ierr = PetscMemzero(X->work,k*sizeof(PetscScalar));CHKERRQ(ierr);
    }
    if (!q) {
      ierr = VecCUDARestoreArrayWrite(X->buffer,&d_work);CHKERRQ(ierr);
      ierr = VecGetArray(X->buffer,&qq);CHKERRQ(ierr);
    } else {
      ierr = cudaFree(d_work);CHKERRQ(ierr);
    }
    ierr = PetscMPIIntCast(k,&len);CHKERRQ(ierr);
    ierr = MPI_Allreduce(X->work,qq,len,MPIU_SCALAR,MPIU_SUM,PetscObjectComm((PetscObject)X));CHKERRQ(ierr);
    if (!q) { ierr = VecRestoreArray(X->buffer,&qq);CHKERRQ(ierr); }
  } else {
    if (n) {
#if defined(PETSC_USE_COMPLEX)
      cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_C,CUBLAS_OP_N,one,k,n,&sone,d_x,n,d_A,n,&szero,d_work,one);CHKERRCUBLAS(cberr);
      ierr = ConjugateCudaArray(d_work,k);CHKERRQ(ierr);
#else
      cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_N,CUBLAS_OP_N,one,k,n,&sone,d_x,one,d_A,n,&szero,d_work,one);CHKERRCUBLAS(cberr);
#endif
    }
    if (!q) {
      ierr = VecCUDARestoreArrayWrite(X->buffer,&d_work);CHKERRQ(ierr);
    } else {
      ierr = cudaMemcpy(q,d_work,k*sizeof(PetscScalar),cudaMemcpyDeviceToHost);CHKERRQ(ierr);
      ierr = cudaFree(d_work);CHKERRQ(ierr);
    }
  }
  ierr = WaitForGPU();CHKERRCUDA(ierr);
  ierr = VecCUDARestoreArrayRead(z,&d_py);CHKERRQ(ierr);
  ierr = VecCUDARestoreArrayRead(x->v,&d_px);CHKERRQ(ierr);
  ierr = PetscLogFlops(2.0*n*k);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
    y := A'*x computed as y' := x'*A
*/
PetscErrorCode BVDotVec_Local_Svec_CUDA(BV X,Vec y,PetscScalar *m)
{
  PetscErrorCode    ierr;
  BV_SVEC           *x = (BV_SVEC*)X->data;
  const PetscScalar *d_A,*d_x,*d_px,*d_py;
  PetscScalar       *d_y,szero=0.0,sone=1.0;
  PetscBLASInt      n,k,one=1;
  Vec               z = y;
  cublasStatus_t    cberr;

  PetscFunctionBegin;
  n = X->n;
  k = X->k-X->l;
  if (X->matrix) {
    ierr = BV_IPMatMult(X,y);CHKERRQ(ierr);
    z = X->Bx;
  }
  ierr = VecCUDAGetArrayRead(x->v,&d_px);CHKERRQ(ierr);
  ierr = VecCUDAGetArrayRead(z,&d_py);CHKERRQ(ierr);
  d_A = d_px+(X->nc+X->l)*X->n;
  d_x = d_py;
  if (n) {
    ierr = cudaMalloc((void**)&d_y,k*sizeof(PetscScalar));CHKERRQ(ierr);
#if defined(PETSC_USE_COMPLEX)
    cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_C,CUBLAS_OP_N,one,k,n,&sone,d_x,n,d_A,n,&szero,d_y,one);CHKERRCUBLAS(cberr);
    ierr = ConjugateCudaArray(d_y,k);CHKERRQ(ierr);
#else
    cberr = cublasXgemm(cublasv2handle,CUBLAS_OP_N,CUBLAS_OP_N,one,k,n,&sone,d_x,one,d_A,n,&szero,d_y,one);CHKERRCUBLAS(cberr);
#endif
    ierr = WaitForGPU();CHKERRCUDA(ierr);
    ierr = cudaMemcpy(m,d_y,k*sizeof(PetscScalar),cudaMemcpyDeviceToHost);CHKERRQ(ierr);
    ierr = cudaFree(d_y);CHKERRQ(ierr);
  }
  ierr = VecCUDARestoreArrayRead(z,&d_py);CHKERRQ(ierr);
  ierr = VecCUDARestoreArrayRead(x->v,&d_px);CHKERRQ(ierr);
  ierr = PetscLogFlops(2.0*n*k);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
    Scale n scalars
*/
PetscErrorCode BVScale_Svec_CUDA(BV bv,PetscInt j,PetscScalar alpha)
{
  PetscErrorCode ierr;
  BV_SVEC        *ctx = (BV_SVEC*)bv->data;
  PetscScalar    *d_array, *d_A;
  PetscBLASInt   n,one=1;
  cublasStatus_t cberr;

  PetscFunctionBegin;
  ierr = VecCUDAGetArrayReadWrite(ctx->v,&d_array);CHKERRQ(ierr);
  if (j<0) {
    d_A = d_array+(bv->nc+bv->l)*bv->n;
    n = (bv->k-bv->l)*bv->n;
  } else {
    d_A = d_array+(bv->nc+j)*bv->n;
    n = bv->n;
  }
  if (alpha == (PetscScalar)0.0) {
    ierr = cudaMemset(d_A,0,n*sizeof(PetscScalar));CHKERRQ(ierr);
  } else if (alpha != (PetscScalar)1.0) {
    cberr = cublasXscal(cublasv2handle,n,&alpha,d_A,one);CHKERRCUBLAS(cberr);
    ierr = WaitForGPU();CHKERRCUDA(ierr);
    ierr = PetscLogFlops(n);CHKERRQ(ierr);
  }
  ierr = VecCUDARestoreArrayReadWrite(ctx->v,&d_array);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode BVMatMult_Svec_CUDA(BV V,Mat A,BV W)
{
  PetscErrorCode    ierr;
  BV_SVEC           *v = (BV_SVEC*)V->data,*w = (BV_SVEC*)W->data;
  const PetscScalar *d_pv;
  PetscScalar       *d_pw;
  PetscInt          j;

  PetscFunctionBegin;
  ierr = VecCUDAGetArrayRead(v->v,&d_pv);CHKERRQ(ierr);
  ierr = VecCUDAGetArrayWrite(w->v,&d_pw);CHKERRQ(ierr);
  for (j=0;j<V->k-V->l;j++) {
    ierr = VecCUDAPlaceArray(V->cv[1],(PetscScalar *)d_pv+(V->nc+V->l+j)*V->n);CHKERRQ(ierr);
    ierr = VecCUDAPlaceArray(W->cv[1],d_pw+(W->nc+W->l+j)*W->n);CHKERRQ(ierr);
    ierr = MatMult(A,V->cv[1],W->cv[1]);CHKERRQ(ierr);
    ierr = VecCUDAResetArray(V->cv[1]);CHKERRQ(ierr);
    ierr = VecCUDAResetArray(W->cv[1]);CHKERRQ(ierr);
  }
  ierr = VecCUDARestoreArrayRead(v->v,&d_pv);CHKERRQ(ierr);
  ierr = VecCUDARestoreArrayWrite(w->v,&d_pw);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode BVCopy_Svec_CUDA(BV V,BV W)
{
  PetscErrorCode    ierr;
  BV_SVEC           *v = (BV_SVEC*)V->data,*w = (BV_SVEC*)W->data;
  const PetscScalar *d_pv,*d_pvc;
  PetscScalar       *d_pw,*d_pwc;
  cudaError_t       err;

  PetscFunctionBegin;
  ierr = VecCUDAGetArrayRead(v->v,&d_pv);CHKERRQ(ierr);
  ierr = VecCUDAGetArrayWrite(w->v,&d_pw);CHKERRQ(ierr);
  d_pvc = d_pv+(V->nc+V->l)*V->n;
  d_pwc = d_pw+(W->nc+W->l)*W->n;
  err = cudaMemcpy(d_pwc,d_pvc,(V->k-V->l)*V->n*sizeof(PetscScalar),cudaMemcpyDeviceToDevice);CHKERRCUDA(err);
  ierr = VecCUDARestoreArrayRead(v->v,&d_pv);CHKERRQ(ierr);
  ierr = VecCUDARestoreArrayWrite(w->v,&d_pw);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode BVCopyColumn_Svec_CUDA(BV V,PetscInt j,PetscInt i)
{
  PetscErrorCode ierr;
  BV_SVEC        *v = (BV_SVEC*)V->data;
  PetscScalar    *d_pv;
  cudaError_t    err;

  PetscFunctionBegin;
  ierr = VecCUDAGetArrayReadWrite(v->v,&d_pv);CHKERRQ(ierr);
  err = cudaMemcpy(d_pv+(V->nc+i)*V->n,d_pv+(V->nc+j)*V->n,V->n*sizeof(PetscScalar),cudaMemcpyDeviceToDevice);CHKERRCUDA(err);
  ierr = VecCUDARestoreArrayReadWrite(v->v,&d_pv);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode BVResize_Svec_CUDA(BV bv,PetscInt m,PetscBool copy)
{
  PetscErrorCode    ierr;
  BV_SVEC           *ctx = (BV_SVEC*)bv->data;
  const PetscScalar *d_pv;
  PetscScalar       *d_pnew,*d_ptr;
  PetscInt          bs,lsplit;
  Vec               vnew,vpar;
  char              str[50];
  cudaError_t       err;
  BV                parent;

  PetscFunctionBegin;
  if (bv->issplit==2) {
    parent = bv->splitparent;
    lsplit = parent->lsplit;
    vpar = ((BV_SVEC*)parent->data)->v;
    ierr = VecCUDAResetArray(ctx->v);CHKERRQ(ierr);
    ierr = VecCUDAGetArrayReadWrite(vpar,&d_ptr);CHKERRQ(ierr);
    ierr = VecCUDAPlaceArray(ctx->v,d_ptr+lsplit*bv->n);CHKERRQ(ierr);
    ierr = VecCUDARestoreArrayReadWrite(vpar,&d_ptr);CHKERRQ(ierr);
  } else if (!bv->issplit) {
    ierr = VecGetBlockSize(bv->t,&bs);CHKERRQ(ierr);
    ierr = VecCreate(PetscObjectComm((PetscObject)bv->t),&vnew);CHKERRQ(ierr);
    ierr = VecSetType(vnew,((PetscObject)bv->t)->type_name);CHKERRQ(ierr);
    ierr = VecSetSizes(vnew,m*bv->n,PETSC_DECIDE);CHKERRQ(ierr);
    ierr = VecSetBlockSize(vnew,bs);CHKERRQ(ierr);
    ierr = PetscLogObjectParent((PetscObject)bv,(PetscObject)vnew);CHKERRQ(ierr);
    if (((PetscObject)bv)->name) {
      ierr = PetscSNPrintf(str,50,"%s_0",((PetscObject)bv)->name);CHKERRQ(ierr);
      ierr = PetscObjectSetName((PetscObject)vnew,str);CHKERRQ(ierr);
    }
    if (copy) {
      ierr = VecCUDAGetArrayRead(ctx->v,&d_pv);CHKERRQ(ierr);
      ierr = VecCUDAGetArrayWrite(vnew,&d_pnew);CHKERRQ(ierr);
      err = cudaMemcpy(d_pnew,d_pv,PetscMin(m,bv->m)*bv->n*sizeof(PetscScalar),cudaMemcpyDeviceToDevice);CHKERRCUDA(err);
      ierr = VecCUDARestoreArrayRead(ctx->v,&d_pv);CHKERRQ(ierr);
      ierr = VecCUDARestoreArrayWrite(vnew,&d_pnew);CHKERRQ(ierr);
    }
    ierr = VecDestroy(&ctx->v);CHKERRQ(ierr);
    ctx->v = vnew;
  }
  PetscFunctionReturn(0);
}

PetscErrorCode BVGetColumn_Svec_CUDA(BV bv,PetscInt j,Vec *v)
{
  PetscErrorCode ierr;
  BV_SVEC        *ctx = (BV_SVEC*)bv->data;
  PetscScalar    *d_pv;
  PetscInt       l;

  PetscFunctionBegin;
  l = BVAvailableVec;
  ierr = VecCUDAGetArrayReadWrite(ctx->v,&d_pv);CHKERRQ(ierr);
  ierr = VecCUDAPlaceArray(bv->cv[l],d_pv+(bv->nc+j)*bv->n);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode BVRestoreColumn_Svec_CUDA(BV bv,PetscInt j,Vec *v)
{
  PetscErrorCode ierr;
  BV_SVEC        *ctx = (BV_SVEC*)bv->data;
  PetscInt       l;

  PetscFunctionBegin;
  l = (j==bv->ci[0])? 0: 1;
  ierr = VecCUDAResetArray(bv->cv[l]);CHKERRQ(ierr);
  ierr = VecCUDARestoreArrayReadWrite(ctx->v,NULL);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode BVRestoreSplit_Svec_CUDA(BV bv,BV *L,BV *R)
{
  PetscErrorCode    ierr;
  Vec               v;
  const PetscScalar *d_pv;
  PetscObjectState  lstate,rstate;
  PetscBool         change=PETSC_FALSE;

  PetscFunctionBegin;
  /* force sync flag to PETSC_CUDA_BOTH */
  if (L) {
    ierr = PetscObjectStateGet((PetscObject)*L,&lstate);CHKERRQ(ierr);
    if (lstate != bv->lstate) {
      v = ((BV_SVEC*)bv->L->data)->v;
      ierr = VecCUDAGetArrayRead(v,&d_pv);CHKERRQ(ierr);
      ierr = VecCUDARestoreArrayRead(v,&d_pv);CHKERRQ(ierr);
      change = PETSC_TRUE;
    }
  }
  if (R) {
    ierr = PetscObjectStateGet((PetscObject)*R,&rstate);CHKERRQ(ierr);
    if (rstate != bv->rstate) {
      v = ((BV_SVEC*)bv->R->data)->v;
      ierr = VecCUDAGetArrayRead(v,&d_pv);CHKERRQ(ierr);
      ierr = VecCUDARestoreArrayRead(v,&d_pv);CHKERRQ(ierr);
      change = PETSC_TRUE;
    }
  }
  if (change) {
    v = ((BV_SVEC*)bv->data)->v;
    ierr = VecCUDAGetArrayReadWrite(v,(PetscScalar **)&d_pv);CHKERRQ(ierr);
    ierr = VecCUDARestoreArrayReadWrite(v,(PetscScalar **)&d_pv);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}
