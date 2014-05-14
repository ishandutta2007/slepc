/*
   BV operations.

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

#include <slepc-private/bvimpl.h>      /*I "slepcbv.h" I*/

#undef __FUNCT__
#define __FUNCT__ "BVMult"
/*@
   BVMult - Computes Y = beta*Y + alpha*X*Q.

   Logically Collective on BV

   Input Parameters:
+  Y,X        - basis vectors
.  alpha,beta - scalars
-  Q          - a sequential dense matrix

   Output Parameter:
.  Y          - the modified basis vectors

   Notes:
   X and Y must be different objects. The case X=Y can be addressed with
   BVMultInPlace().

   The matrix Q must be a sequential dense Mat, with all entries equal on
   all processes (otherwise each process will compute a different update).

   The leading columns of Y are not modified. Also, if X has leading
   columns specified, then these columns do not participate in the computation.

   Level: intermediate

.seealso: BVMultVec(), BVMultInPlace(), BVSetActiveColumns()
@*/
PetscErrorCode BVMult(BV Y,PetscScalar alpha,PetscScalar beta,BV X,Mat Q)
{
  PetscErrorCode ierr;
  PetscBool      match;
  PetscInt       m,n;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(Y,BV_CLASSID,1);
  PetscValidLogicalCollectiveScalar(Y,alpha,2);
  PetscValidLogicalCollectiveScalar(Y,beta,3);
  PetscValidHeaderSpecific(X,BV_CLASSID,4);
  PetscValidHeaderSpecific(Q,MAT_CLASSID,5);
  PetscValidType(Y,1);
  BVCheckSizes(Y,1);
  PetscValidType(X,4);
  BVCheckSizes(X,4);
  PetscValidType(Q,5);
  PetscCheckSameTypeAndComm(Y,1,X,4);
  if (X==Y) SETERRQ(PetscObjectComm((PetscObject)Y),PETSC_ERR_ARG_WRONG,"X and Y arguments must be different");
  ierr = PetscObjectTypeCompare((PetscObject)Q,MATSEQDENSE,&match);CHKERRQ(ierr);
  if (!match) SETERRQ(PetscObjectComm((PetscObject)Y),PETSC_ERR_SUP,"Mat argument must be of type seqdense");

  ierr = MatGetSize(Q,&m,&n);CHKERRQ(ierr);
  if (m!=X->k) SETERRQ2(PetscObjectComm((PetscObject)Y),PETSC_ERR_ARG_SIZ,"Mat argument has %D rows, cannot multiply a BV with %D active columns",m,X->k);
  if (n!=Y->k) SETERRQ2(PetscObjectComm((PetscObject)Y),PETSC_ERR_ARG_SIZ,"Mat argument has %D columns, result cannot be added to a BV with %D active columns",n,Y->k);
  if (X->n!=Y->n) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_ARG_INCOMP,"Mismatching local dimension X %D, Y %D",X->n,Y->n);
  if (!X->n) PetscFunctionReturn(0);

  ierr = PetscLogEventBegin(BV_Mult,X,Y,0,0);CHKERRQ(ierr);
  ierr = (*Y->ops->mult)(Y,alpha,beta,X,Q);CHKERRQ(ierr);
  ierr = PetscLogEventEnd(BV_Mult,X,Y,0,0);CHKERRQ(ierr);
  ierr = PetscObjectStateIncrease((PetscObject)Y);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "BVMultVec"
/*@
   BVMultVec - Computes y = beta*y + alpha*X*q.

   Logically Collective on BV and Vec

   Input Parameters:
+  X          - a basis vectors object
.  alpha,beta - scalars
.  y          - a vector
-  q          - an array of scalars

   Output Parameter:
.  y          - the modified vector

   Notes:
   This operation is the analogue of BVMult() but with a BV and a Vec,
   instead of two BV. Note that arguments are listed in different order
   with respect to BVMult().

   If X has leading columns specified, then these columns do not participate
   in the computation.

   The length of array q must be equal to the number of active columns of X
   minus the number of leading columns, i.e. the first entry of q multiplies
   the first non-leading column.

   Level: intermediate

.seealso: BVMult(), BVMultInPlace(), BVSetActiveColumns()
@*/
PetscErrorCode BVMultVec(BV X,PetscScalar alpha,PetscScalar beta,Vec y,PetscScalar *q)
{
  PetscErrorCode ierr;
  PetscInt       n,N;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(X,BV_CLASSID,1);
  PetscValidLogicalCollectiveScalar(X,alpha,2);
  PetscValidLogicalCollectiveScalar(X,beta,3);
  PetscValidHeaderSpecific(y,VEC_CLASSID,4);
  PetscValidPointer(q,5);
  PetscValidType(X,1);
  BVCheckSizes(X,1);
  PetscValidType(y,4);
  PetscCheckSameComm(X,1,y,4);

  ierr = VecGetSize(y,&N);CHKERRQ(ierr);
  ierr = VecGetLocalSize(y,&n);CHKERRQ(ierr);
  if (N!=X->N || n!=X->n) SETERRQ4(PetscObjectComm((PetscObject)X),PETSC_ERR_ARG_INCOMP,"Vec sizes (global %D, local %D) do not match BV sizes (global %D, local %D)",N,n,X->N,X->n);
  if (!X->n) PetscFunctionReturn(0);

  ierr = PetscLogEventBegin(BV_Mult,X,y,0,0);CHKERRQ(ierr);
  ierr = (*X->ops->multvec)(X,alpha,beta,y,q);CHKERRQ(ierr);
  ierr = PetscLogEventEnd(BV_Mult,X,y,0,0);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "BVMultInPlace"
/*@
   BVMultInPlace - Update a set of vectors as V(:,s:e-1) = V*Q(:,s:e-1).

   Logically Collective on BV

   Input Parameters:
+  Q - a sequential dense matrix
.  s - first column of V to be overwritten
-  e - first column of V not to be overwritten

   Input/Output Parameter:
+  V - basis vectors

   Notes:
   The matrix Q must be a sequential dense Mat, with all entries equal on
   all processes (otherwise each process will compute a different update).

   This function computes V(:,s:e-1) = V*Q(:,s:e-1), that is, given a set of
   vectors V, columns from s to e-1 are overwritten with columns from s to
   e-1 of the matrix-matrix product V*Q. Only columns s to e-1 of Q are
   referenced.

   Level: intermediate

.seealso: BVMult(), BVMultVec(), BVMultInPlaceTranspose(), BVSetActiveColumns()
@*/
PetscErrorCode BVMultInPlace(BV V,Mat Q,PetscInt s,PetscInt e)
{
  PetscErrorCode ierr;
  PetscBool      match;
  PetscInt       m,n;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(V,BV_CLASSID,1);
  PetscValidHeaderSpecific(Q,MAT_CLASSID,2);
  PetscValidLogicalCollectiveInt(V,s,3);
  PetscValidLogicalCollectiveInt(V,e,4);
  PetscValidType(V,1);
  BVCheckSizes(V,1);
  PetscValidType(Q,2);
  ierr = PetscObjectTypeCompare((PetscObject)Q,MATSEQDENSE,&match);CHKERRQ(ierr);
  if (!match) SETERRQ(PetscObjectComm((PetscObject)V),PETSC_ERR_SUP,"Mat argument must be of type seqdense");

  if (s<V->l || s>=V->k) SETERRQ3(PetscObjectComm((PetscObject)V),PETSC_ERR_ARG_OUTOFRANGE,"Argument s has wrong value %D, should be between %D and %D",s,V->l,V->k-1);
  if (e<V->l || e>V->k) SETERRQ3(PetscObjectComm((PetscObject)V),PETSC_ERR_ARG_OUTOFRANGE,"Argument e has wrong value %D, should be between %D and %D",e,V->l,V->k);
  ierr = MatGetSize(Q,&m,&n);CHKERRQ(ierr);
  if (m!=V->k) SETERRQ2(PetscObjectComm((PetscObject)V),PETSC_ERR_ARG_SIZ,"Mat argument has %D rows, cannot multiply a BV with %D active columns",m,V->k);
  if (e>n) SETERRQ2(PetscObjectComm((PetscObject)V),PETSC_ERR_ARG_SIZ,"Mat argument only has %D columns, the requested value of e is larger: %D",n,e);
  if (s>=e || !V->n) PetscFunctionReturn(0);

  ierr = PetscLogEventBegin(BV_Mult,V,Q,0,0);CHKERRQ(ierr);
  ierr = (*V->ops->multinplace)(V,Q,s,e);CHKERRQ(ierr);
  ierr = PetscLogEventEnd(BV_Mult,V,Q,0,0);CHKERRQ(ierr);
  ierr = PetscObjectStateIncrease((PetscObject)V);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "BVMultInPlaceTranspose"
/*@
   BVMultInPlaceTranspose - Update a set of vectors as V(:,s:e-1) = V*Q'(:,s:e-1).

   Logically Collective on BV

   Input Parameters:
+  Q - a sequential dense matrix
.  s - first column of V to be overwritten
-  e - first column of V not to be overwritten

   Input/Output Parameter:
+  V - basis vectors

   Notes:
   This is a variant of BVMultInPlace() where the conjugate transpose
   of Q is used.

   Level: intermediate

.seealso: BVMultInPlace()
@*/
PetscErrorCode BVMultInPlaceTranspose(BV V,Mat Q,PetscInt s,PetscInt e)
{
  PetscErrorCode ierr;
  PetscBool      match;
  PetscInt       m,n;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(V,BV_CLASSID,1);
  PetscValidHeaderSpecific(Q,MAT_CLASSID,2);
  PetscValidLogicalCollectiveInt(V,s,3);
  PetscValidLogicalCollectiveInt(V,e,4);
  PetscValidType(V,1);
  BVCheckSizes(V,1);
  PetscValidType(Q,2);
  ierr = PetscObjectTypeCompare((PetscObject)Q,MATSEQDENSE,&match);CHKERRQ(ierr);
  if (!match) SETERRQ(PetscObjectComm((PetscObject)V),PETSC_ERR_SUP,"Mat argument must be of type seqdense");

  if (s<V->l || s>=V->k) SETERRQ3(PetscObjectComm((PetscObject)V),PETSC_ERR_ARG_OUTOFRANGE,"Argument s has wrong value %D, should be between %D and %D",s,V->l,V->k-1);
  if (e<V->l || e>V->k) SETERRQ3(PetscObjectComm((PetscObject)V),PETSC_ERR_ARG_OUTOFRANGE,"Argument e has wrong value %D, should be between %D and %D",e,V->l,V->k);
  ierr = MatGetSize(Q,&m,&n);CHKERRQ(ierr);
  if (n!=V->k) SETERRQ2(PetscObjectComm((PetscObject)V),PETSC_ERR_ARG_SIZ,"Mat argument has %D rows, cannot multiply a BV with %D active columns",n,V->k);
  if (e>m) SETERRQ2(PetscObjectComm((PetscObject)V),PETSC_ERR_ARG_SIZ,"Mat argument only has %D columns, the requested value of e is larger: %D",m,e);
  if (s>=e || !V->n) PetscFunctionReturn(0);

  ierr = PetscLogEventBegin(BV_Mult,V,Q,0,0);CHKERRQ(ierr);
  ierr = (*V->ops->multinplacetrans)(V,Q,s,e);CHKERRQ(ierr);
  ierr = PetscLogEventEnd(BV_Mult,V,Q,0,0);CHKERRQ(ierr);
  ierr = PetscObjectStateIncrease((PetscObject)V);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "BVDot"
/*@
   BVDot - Computes the 'block-dot' product of two basis vectors objects.

   Collective on BV

   Input Parameters:
+  X, Y - basis vectors
-  M    - Mat object where the result must be placed

   Output Parameter:
.  M    - the resulting matrix

   Notes:
   This is the generalization of VecDot() for a collection of vectors, M = Y^H*X.
   The result is a matrix M whose entry m_ij is equal to y_i^H x_j (where y_i^H
   denotes the conjugate transpose of y_i).

   If a non-standard inner product has been specified with BVSetMatrix(),
   then the result is M = Y^H*B*X. In this case, both X and Y must have
   the same associated matrix.

   On entry, M must be a sequential dense Mat with dimensions m,n where
   m is the number of active columns of Y and n is the number of active columns of X.
   Only rows (resp. columns) of M starting from ly (resp. lx) are computed,
   where ly (resp. lx) is the number of leading columns of Y (resp. X).

   X and Y need not be different objects.

   Level: intermediate

.seealso: BVDotVec(), BVSetActiveColumns(), BVSetMatrix()
@*/
PetscErrorCode BVDot(BV X,BV Y,Mat M)
{
  PetscErrorCode ierr;
  PetscBool      match;
  PetscInt       j,m,n;
  PetscScalar    *marray;
  Vec            z;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(X,BV_CLASSID,1);
  PetscValidHeaderSpecific(Y,BV_CLASSID,2);
  PetscValidHeaderSpecific(M,MAT_CLASSID,3);
  PetscValidType(X,1);
  BVCheckSizes(X,1);
  PetscValidType(Y,2);
  BVCheckSizes(Y,2);
  PetscValidType(M,3);
  PetscCheckSameTypeAndComm(X,1,Y,2);
  ierr = PetscObjectTypeCompare((PetscObject)M,MATSEQDENSE,&match);CHKERRQ(ierr);
  if (!match) SETERRQ(PetscObjectComm((PetscObject)X),PETSC_ERR_SUP,"Mat argument must be of type seqdense");

  ierr = MatGetSize(M,&m,&n);CHKERRQ(ierr);
  if (m!=Y->k) SETERRQ2(PetscObjectComm((PetscObject)X),PETSC_ERR_ARG_SIZ,"Mat argument has %D rows, should be %D",m,Y->k);
  if (n!=X->k) SETERRQ2(PetscObjectComm((PetscObject)X),PETSC_ERR_ARG_SIZ,"Mat argument has %D columns, should be %D",n,X->k);
  if (X->n!=Y->n) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_ARG_INCOMP,"Mismatching local dimension X %D, Y %D",X->n,Y->n);
  if (X->matrix!=Y->matrix) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_ARG_WRONGSTATE,"X and Y must have the same inner product matrix");
  if (!X->n) PetscFunctionReturn(0);

  ierr = PetscLogEventBegin(BV_Dot,X,Y,0,0);CHKERRQ(ierr);
  if (X->matrix) { /* non-standard inner product: cast into dotvec ops */
    ierr = MatDenseGetArray(M,&marray);CHKERRQ(ierr);
    for (j=X->l;j<X->k;j++) {
      ierr = BVGetColumn(X,j,&z);CHKERRQ(ierr);
      ierr = (*X->ops->dotvec)(Y,z,marray+j*m+Y->l);CHKERRQ(ierr);
      ierr = BVRestoreColumn(X,j,&z);CHKERRQ(ierr);
    }
    ierr = MatDenseRestoreArray(M,&marray);CHKERRQ(ierr);
  } else {
    ierr = (*X->ops->dot)(X,Y,M);CHKERRQ(ierr);
  }
  ierr = PetscLogEventEnd(BV_Dot,X,Y,0,0);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "BVDotVec"
/*@
   BVDotVec - Computes multiple dot products of a vector against all the
   column vectors of a BV.

   Collective on BV and Vec

   Input Parameters:
+  X - basis vectors
-  y - a vector

   Output Parameter:
.  m - an array where the result must be placed

   Notes:
   This is analogue to VecMDot(), but using BV to represent a collection
   of vectors. The result is m = X^H*y, so m_i is equal to x_j^H y. Note
   that here X is transposed as opposed to BVDot().

   If a non-standard inner product has been specified with BVSetMatrix(),
   then the result is m = X^H*B*y.

   The length of array m must be equal to the number of active columns of X
   minus the number of leading columns, i.e. the first entry of m is the
   product of the first non-leading column with y.

   Level: intermediate

.seealso: BVDot(), BVSetActiveColumns(), BVSetMatrix()
@*/
PetscErrorCode BVDotVec(BV X,Vec y,PetscScalar *m)
{
  PetscErrorCode ierr;
  PetscInt       n;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(X,BV_CLASSID,1);
  PetscValidHeaderSpecific(y,VEC_CLASSID,2);
  PetscValidType(X,1);
  BVCheckSizes(X,1);
  PetscValidType(y,2);
  PetscCheckSameTypeAndComm(X,1,y,2);

  ierr = VecGetLocalSize(y,&n);CHKERRQ(ierr);
  if (X->n!=n) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_ARG_INCOMP,"Mismatching local dimension X %D, y %D",X->n,n);
  if (!X->n) PetscFunctionReturn(0);

  ierr = PetscLogEventBegin(BV_Dot,X,y,0,0);CHKERRQ(ierr);
  ierr = (*X->ops->dotvec)(X,y,m);CHKERRQ(ierr);
  ierr = PetscLogEventEnd(BV_Dot,X,y,0,0);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "BVScale"
/*@
   BVScale - Scale one column (or all columns) of a BV.

   Logically Collective on BV

   Input Parameters:
+  bv    - basis vectors
-  j     - column number to be scaled (or negative number to scale all columns)
-  alpha - scaling factor

   Note:
   The column index j must be smaller than the number of active columns.
   If j<0 then all active columns are scaled.

   Level: intermediate

.seealso: BVSetActiveColumns()
@*/
PetscErrorCode BVScale(BV bv,PetscInt j,PetscScalar alpha)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(bv,BV_CLASSID,1);
  PetscValidLogicalCollectiveInt(bv,j,2);
  PetscValidLogicalCollectiveScalar(bv,alpha,3);
  PetscValidType(bv,1);
  BVCheckSizes(bv,1);

  if (j>=bv->k) SETERRQ2(PetscObjectComm((PetscObject)bv),PETSC_ERR_ARG_OUTOFRANGE,"Argument j has wrong value %D, the number of active columns is %D",j,bv->k);
  if (!bv->n || alpha == (PetscScalar)1.0) PetscFunctionReturn(0);

  ierr = PetscLogEventBegin(BV_Scale,bv,0,0,0);CHKERRQ(ierr);
  ierr = (*bv->ops->scale)(bv,j,alpha);CHKERRQ(ierr);
  ierr = PetscLogEventEnd(BV_Scale,bv,0,0,0);CHKERRQ(ierr);
  ierr = PetscObjectStateIncrease((PetscObject)bv);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "BVNorm"
/*@
   BVNorm - Computes the vector norm of a selected column, or the matrix norm
   of all columns.

   Collective on BV

   Input Parameters:
+  bv   - basis vectors
-  j    - column number to be used (or negative number to select all columns)
-  type - the norm type

   Output Parameter:
.  val  - the norm

   Notes:
   The column index j must be smaller than the number of active columns.

   If j<0 then all active columns are considered as a matrix. In this case, the
   allowed norms are NORM_1, NORM_FROBENIUS, and NORM_INFINITY. Otherwise, the
   norm of V[j] is computed (NORM_1, NORM_2, or NORM_INFINITY).

   If a non-standard inner product has been specified with BVSetMatrix(),
   then j<0 is not allowed and the returned value is sqrt(V[j]'*B*V[j]), 
   where B is the inner product matrix (argument 'type' is ignored).

   Level: intermediate

.seealso: BVSetActiveColumns(), BVSetMatrix()
@*/
PetscErrorCode BVNorm(BV bv,PetscInt j,NormType type,PetscReal *val)
{
  PetscErrorCode ierr;
  Vec            z;
  PetscScalar    p;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(bv,BV_CLASSID,1);
  PetscValidLogicalCollectiveInt(bv,j,2);
  PetscValidLogicalCollectiveEnum(bv,type,3);
  PetscValidPointer(val,4);
  PetscValidType(bv,1);
  BVCheckSizes(bv,1);

  if (j>=bv->k) SETERRQ2(PetscObjectComm((PetscObject)bv),PETSC_ERR_ARG_OUTOFRANGE,"Argument j has wrong value %D, the number of active columns is %D",j,bv->k);
  if (type==NORM_1_AND_2 || (type==NORM_2 && j<0)) SETERRQ(PetscObjectComm((PetscObject)bv),PETSC_ERR_SUP,"Requested norm not available");
  if (bv->matrix && j<0) SETERRQ(PetscObjectComm((PetscObject)bv),PETSC_ERR_SUP,"Matrix norm not available for non-standard inner product");

  ierr = PetscLogEventBegin(BV_Norm,bv,0,0,0);CHKERRQ(ierr);
  if (bv->matrix) { /* non-standard inner product */
    ierr = BVGetColumn(bv,j,&z);CHKERRQ(ierr);
    ierr = BV_MatMult(bv,z);CHKERRQ(ierr);
    ierr = VecDot(bv->Bx,z,&p);CHKERRQ(ierr);
    ierr = BVRestoreColumn(bv,j,&z);CHKERRQ(ierr);
    if (PetscAbsScalar(p)<PETSC_MACHINE_EPSILON)
      ierr = PetscInfo(bv,"Zero norm, either the vector is zero or a semi-inner product is being used\n");CHKERRQ(ierr);
    if (bv->indef) {
      if (PetscAbsReal(PetscImaginaryPart(p))/PetscAbsScalar(p)>PETSC_MACHINE_EPSILON) SETERRQ(PetscObjectComm((PetscObject)bv),1,"BVNorm: The inner product is not well defined");
      if (PetscRealPart(p)<0.0) *val = -PetscSqrtScalar(-PetscRealPart(p));
      else *val = PetscSqrtScalar(PetscRealPart(p));
    } else { 
      if (PetscRealPart(p)<0.0 || PetscAbsReal(PetscImaginaryPart(p))/PetscAbsScalar(p)>PETSC_MACHINE_EPSILON) SETERRQ(PetscObjectComm((PetscObject)bv),1,"BVNorm: The inner product is not well defined");
      *val = PetscSqrtScalar(PetscRealPart(p));
    }
  } else {
    ierr = (*bv->ops->norm)(bv,j,type,val);CHKERRQ(ierr);
  }
  ierr = PetscLogEventEnd(BV_Norm,bv,0,0,0);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "BVSetRandom"
/*@
   BVSetRandom - Set one column (or all columns) of a BV to random numbers.

   Logically Collective on BV

   Input Parameters:
+  bv    - basis vectors
-  j     - column number to be set (or negative number to set all columns)
-  rctx - the random number context, formed by PetscRandomCreate(), or NULL and
          it will create one internally.

   Note:
   The column index j must be smaller than the number of active columns.
   If j<0 then all active columns are scaled.

   This operation is analogue to VecSetRandom - the difference is that the
   generated random vector is the same irrespective of the size of the
   communicator (if all processes pass a PetscRandom context initialized
   with the same seed).

   Level: advanced

.seealso: BVSetActiveColumns()
@*/
PetscErrorCode BVSetRandom(BV bv,PetscInt j,PetscRandom rctx)
{
  PetscErrorCode ierr;
  PetscRandom    rand=NULL;
  PetscInt       i,low,high,k,ks,ke;
  PetscScalar    *px,t;
  Vec            x;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(bv,BV_CLASSID,1);
  PetscValidLogicalCollectiveInt(bv,j,2);
  if (rctx) PetscValidHeaderSpecific(rctx,PETSC_RANDOM_CLASSID,3);
  else {
    ierr = PetscRandomCreate(PetscObjectComm((PetscObject)bv),&rand);CHKERRQ(ierr);
    ierr = PetscRandomSetSeed(rand,0x12345678);CHKERRQ(ierr);
    ierr = PetscRandomSetFromOptions(rand);CHKERRQ(ierr);
    rctx = rand;
  }
  PetscValidType(bv,1);
  BVCheckSizes(bv,1);
  if (j>=bv->k) SETERRQ2(PetscObjectComm((PetscObject)bv),PETSC_ERR_ARG_OUTOFRANGE,"Argument j has wrong value %D, the number of active columns is %D",j,bv->k);

  ierr = PetscLogEventBegin(BV_SetRandom,bv,rctx,0,0);CHKERRQ(ierr);
  if (j<0) {
    ks = 0; ke = bv->k;
  } else {
    ks = j; ke = j+1;
  }
  for (k=ks;k<ke;k++) {
    ierr = BVGetColumn(bv,k,&x);CHKERRQ(ierr);
    ierr = VecGetOwnershipRange(x,&low,&high);CHKERRQ(ierr);
    ierr = VecGetArray(x,&px);CHKERRQ(ierr);
    for (i=0;i<bv->N;i++) {
      ierr = PetscRandomGetValue(rctx,&t);CHKERRQ(ierr);
      if (i>=low && i<high) px[i-low] = t;
    }
    ierr = VecRestoreArray(x,&px);CHKERRQ(ierr);
    ierr = BVRestoreColumn(bv,k,&x);CHKERRQ(ierr);
  }
  ierr = PetscLogEventEnd(BV_SetRandom,bv,rctx,0,0);CHKERRQ(ierr);
  ierr = PetscRandomDestroy(&rand);CHKERRQ(ierr);
  ierr = PetscObjectStateIncrease((PetscObject)bv);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

