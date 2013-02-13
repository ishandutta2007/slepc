/*
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   SLEPc - Scalable Library for Eigenvalue Problem Computations
   Copyright (c) 2002-2012, Universitat Politecnica de Valencia, Spain

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

#include <petsc-private/fortranimpl.h>
#include <slepc-private/slepcimpl.h>
#include <slepcsvd.h>

#if defined(PETSC_HAVE_FORTRAN_CAPS)
#define svddestroy_                  SVDDESTROY
#define svdmonitorall_               SVDMONITORALL
#define svdmonitorlg_                SVDMONITORLG
#define svdmonitorlgall_             SVDMONITORLGALL
#define svdmonitorconverged_         SVDMONITORCONVERGED
#define svdmonitorfirst_             SVDMONITORFIRST
#define svdview_                     SVDVIEW
#define svdcreate_                   SVDCREATE
#define svdsettype_                  SVDSETTYPE
#define svdgettype_                  SVDGETTYPE
#define svdgetip_                    SVDGETIP
#define svdgetds_                    SVDGETDS
#define svdmonitorset_               SVDMONITORSET
#define svdgettransposemode_         SVDGETTRANSPOSEMODE
#define svdgetwhichsingulartriplets_ SVDGETWHICHSINGULARTRIPLETS
#define svdsetoptionsprefix_         SVDSETOPTIONSPREFIX
#define svdappendoptionsprefix_      SVDAPPENDOPTIONSPREFIX
#define svdgetoptionsprefix_         SVDGETOPTIONSPREFIX
#define svdgetconvergedreason_       SVDGETCONVERGEDREASON
#elif !defined(PETSC_HAVE_FORTRAN_UNDERSCORE)
#define svddestroy_                  svddestroy
#define svdmonitorall_               svdmonitorall
#define svdmonitorlgall_             svdmonitorlgall
#define svdmonitorconverged_         svdmonitorconverged
#define svdmonitorfirst_             svdmonitorfirst
#define svdview_                     svdview
#define svdcreate_                   svdcreate
#define svdsettype_                  svdsettype
#define svdgettype_                  svdgettype
#define svdgetip_                    svdgetip
#define svdgetds_                    svdgetds
#define svdmonitorset_               svdmonitorset
#define svdgettransposemode_         svdgettransposemode
#define svdgetwhichsingulartriplets_ svdgetwhichsingulartriplets
#define svdsetoptionsprefix_         svdsetoptionsprefix
#define svdappendoptionsprefix_      svdappendoptionsprefix
#define svdgetoptionsprefix_         svdgetoptionsprefix
#define svdgetconvergedreason_       svdgetconvergedreason
#endif

EXTERN_C_BEGIN

/*
   These are not usually called from Fortran but allow Fortran users 
   to transparently set these monitors from .F code, hence no STDCALL
*/
void svdmonitorall_(SVD *svd,PetscInt *it,PetscInt *nconv,PetscReal *sigma,PetscReal *errest,PetscInt *nest,void *ctx,PetscErrorCode *ierr)
{
  *ierr = SVDMonitorAll(*svd,*it,*nconv,sigma,errest,*nest,ctx);
}

void svdmonitorlg_(SVD *svd,PetscInt *it,PetscInt *nconv,PetscReal *sigma,PetscReal *errest,PetscInt *nest,void *ctx,PetscErrorCode *ierr)
{
  *ierr = SVDMonitorLG(*svd,*it,*nconv,sigma,errest,*nest,ctx);
}

void svdmonitorlgall_(SVD *svd,PetscInt *it,PetscInt *nconv,PetscReal *sigma,PetscReal *errest,PetscInt *nest,void *ctx,PetscErrorCode *ierr)
{
  *ierr = SVDMonitorLGAll(*svd,*it,*nconv,sigma,errest,*nest,ctx);
}

void svdmonitorconverged_(SVD *svd,PetscInt *it,PetscInt *nconv,PetscReal *sigma,PetscReal *errest,PetscInt *nest,void *ctx,PetscErrorCode *ierr)
{
  *ierr = SVDMonitorConverged(*svd,*it,*nconv,sigma,errest,*nest,ctx);
}

void svdmonitorfirst_(SVD *svd,PetscInt *it,PetscInt *nconv,PetscReal *sigma,PetscReal *errest,PetscInt *nest,void *ctx,PetscErrorCode *ierr)
{
  *ierr = SVDMonitorFirst(*svd,*it,*nconv,sigma,errest,*nest,ctx);
}

EXTERN_C_END

static struct {
  PetscFortranCallbackId monitor;
  PetscFortranCallbackId monitordestroy;
} _cb;

/* These are not extern C because they are passed into non-extern C user level functions */
#undef __FUNCT__
#define __FUNCT__ "ourmonitor"
static PetscErrorCode ourmonitor(SVD svd,PetscInt i,PetscInt nc,PetscReal *sigma,PetscReal *d,PetscInt l,void* ctx)
{
  PetscObjectUseFortranCallback(svd,_cb.monitor,(SVD*,PetscInt*,PetscInt*,PetscReal*,PetscReal*,PetscInt*,void*,PetscErrorCode*),(&svd,&i,&nc,sigma,d,&l,_ctx,&ierr));
  return 0;
}

#undef __FUNCT__
#define __FUNCT__ "ourdestroy"
static PetscErrorCode ourdestroy(void** ctx)
{
  SVD svd = (SVD)*ctx;
  PetscObjectUseFortranCallback(svd,_cb.monitordestroy,(void*,PetscErrorCode*),(_ctx,&ierr));
  return 0;
}

EXTERN_C_BEGIN

void PETSC_STDCALL svddestroy_(SVD *svd,PetscErrorCode *ierr)
{
  *ierr = SVDDestroy(svd);
}

void PETSC_STDCALL svdview_(SVD *svd,PetscViewer *viewer,PetscErrorCode *ierr)
{
  PetscViewer v;
  PetscPatchDefaultViewers_Fortran(viewer,v);
  *ierr = SVDView(*svd,v);
}

void PETSC_STDCALL svdcreate_(MPI_Fint *comm,SVD *svd,PetscErrorCode *ierr)
{
  *ierr = SVDCreate(MPI_Comm_f2c(*(comm)),svd);
}

void PETSC_STDCALL svdsettype_(SVD *svd,CHAR type PETSC_MIXED_LEN(len),PetscErrorCode *ierr PETSC_END_LEN(len))
{
  char *t;

  FIXCHAR(type,len,t);
  *ierr = SVDSetType(*svd,t);
  FREECHAR(type,t);
}

void PETSC_STDCALL svdgettype_(SVD *svd,CHAR name PETSC_MIXED_LEN(len),PetscErrorCode *ierr PETSC_END_LEN(len))
{
  SVDType tname;

  *ierr = SVDGetType(*svd,&tname);if (*ierr) return;
  *ierr = PetscStrncpy(name,tname,len);
  FIXRETURNCHAR(PETSC_TRUE,name,len);
}

void PETSC_STDCALL svdgetip_(SVD *svd,IP *ip,PetscErrorCode *ierr)
{
  *ierr = SVDGetIP(*svd,ip);
}

void PETSC_STDCALL svdgetds_(SVD *svd,DS *ds,PetscErrorCode *ierr)
{
  *ierr = SVDGetDS(*svd,ds);
}

void PETSC_STDCALL svdmonitorset_(SVD *svd,void (PETSC_STDCALL *monitor)(SVD*,PetscInt*,PetscInt*,PetscReal*,PetscReal*,PetscInt*,void*,PetscErrorCode*),void *mctx,void (PETSC_STDCALL *monitordestroy)(void *,PetscErrorCode*),PetscErrorCode *ierr)
{
  SlepcConvMonitor ctx;

  CHKFORTRANNULLOBJECT(mctx);
  CHKFORTRANNULLFUNCTION(monitordestroy);
  if ((PetscVoidFunction)monitor == (PetscVoidFunction)svdmonitorall_) {
    *ierr = SVDMonitorSet(*svd,SVDMonitorAll,0,0);
  } else if ((PetscVoidFunction)monitor == (PetscVoidFunction)svdmonitorlg_) {
    *ierr = SVDMonitorSet(*svd,SVDMonitorLG,0,0);
  } else if ((PetscVoidFunction)monitor == (PetscVoidFunction)svdmonitorlgall_) {
    *ierr = SVDMonitorSet(*svd,SVDMonitorLGAll,0,0);
  } else if ((PetscVoidFunction)monitor == (PetscVoidFunction)svdmonitorconverged_) {
    if (!FORTRANNULLOBJECT(mctx)) {
      PetscError(PetscObjectComm((PetscObject)*svd),__LINE__,"svdmonitorset_",__FILE__,__SDIR__,PETSC_ERR_ARG_WRONG,PETSC_ERROR_INITIAL,"Must provide PETSC_NULL_OBJECT as a context in the Fortran interface to SVDMonitorSet");
      *ierr = 1;
      return;
    }
    *ierr = PetscNew(struct _n_SlepcConvMonitor,&ctx);
    if (*ierr) return;
    ctx->viewer = NULL;
    *ierr = SVDMonitorSet(*svd,SVDMonitorConverged,ctx,(PetscErrorCode (*)(void**))SlepcConvMonitorDestroy);
  } else if ((PetscVoidFunction)monitor == (PetscVoidFunction)svdmonitorfirst_) {
    *ierr = SVDMonitorSet(*svd,SVDMonitorFirst,0,0);
  } else {
    *ierr = PetscObjectSetFortranCallback((PetscObject)*svd,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.monitor,(PetscVoidFunction)monitor,mctx); if (*ierr) return;
    if (!monitordestroy) {
      *ierr = SVDMonitorSet(*svd,ourmonitor,*svd,0);
    } else {
      *ierr = PetscObjectSetFortranCallback((PetscObject)*svd,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.monitordestroy,(PetscVoidFunction)monitordestroy,mctx); if (*ierr) return;
      *ierr = SVDMonitorSet(*svd,ourmonitor,*svd,ourdestroy);
    }
  }
}

void PETSC_STDCALL svdgettransposemode_(SVD *svd,SVDTransposeMode *mode,PetscErrorCode *ierr)
{
  *ierr = SVDGetTransposeMode(*svd,mode);
}

void PETSC_STDCALL svdgetwhichsingulartriplets_(SVD *svd,SVDWhich *which,PetscErrorCode *ierr)
{
  *ierr = SVDGetWhichSingularTriplets(*svd,which);
}

void PETSC_STDCALL svdsetoptionsprefix_(SVD *svd,CHAR prefix PETSC_MIXED_LEN(len),PetscErrorCode *ierr PETSC_END_LEN(len))
{
  char *t;

  FIXCHAR(prefix,len,t);
  *ierr = SVDSetOptionsPrefix(*svd,t);
  FREECHAR(prefix,t);
}

void PETSC_STDCALL svdappendoptionsprefix_(SVD *svd,CHAR prefix PETSC_MIXED_LEN(len),PetscErrorCode *ierr PETSC_END_LEN(len))
{
  char *t;

  FIXCHAR(prefix,len,t);
  *ierr = SVDAppendOptionsPrefix(*svd,t);
  FREECHAR(prefix,t);
}

void PETSC_STDCALL svdgetoptionsprefix_(SVD *svd,CHAR prefix PETSC_MIXED_LEN(len),PetscErrorCode *ierr PETSC_END_LEN(len))
{
  const char *tname;

  *ierr = SVDGetOptionsPrefix(*svd,&tname); if (*ierr) return;
  *ierr = PetscStrncpy(prefix,tname,len);
}

void PETSC_STDCALL svdgetconvergedreason_(SVD *svd,SVDConvergedReason *reason,PetscErrorCode *ierr)
{
  *ierr = SVDGetConvergedReason(*svd,reason);
}

EXTERN_C_END
