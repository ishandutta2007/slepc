/*
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   SLEPc - Scalable Library for Eigenvalue Problem Computations
   Copyright (c) 2002-2021, Universitat Politecnica de Valencia, Spain

   This file is part of SLEPc.
   SLEPc is distributed under a 2-clause BSD license (see LICENSE).
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
*/
/*
   The SVD routines related to various viewers
*/

#include <slepc/private/svdimpl.h>      /*I "slepcsvd.h" I*/
#include <petscdraw.h>

/*@C
   SVDView - Prints the SVD data structure.

   Collective on svd

   Input Parameters:
+  svd - the singular value solver context
-  viewer - optional visualization context

   Options Database Key:
.  -svd_view -  Calls SVDView() at end of SVDSolve()

   Note:
   The available visualization contexts include
+     PETSC_VIEWER_STDOUT_SELF - standard output (default)
-     PETSC_VIEWER_STDOUT_WORLD - synchronized standard
         output where only the first processor opens
         the file.  All other processors send their
         data to the first processor to print.

   The user can open an alternative visualization context with
   PetscViewerASCIIOpen() - output to a specified file.

   Level: beginner

.seealso: EPSView()
@*/
PetscErrorCode SVDView(SVD svd,PetscViewer viewer)
{
  PetscErrorCode ierr;
  const char     *type=NULL;
  PetscBool      isascii,isshell;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(svd,SVD_CLASSID,1);
  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(PetscObjectComm((PetscObject)svd),&viewer);CHKERRQ(ierr);
  }
  PetscValidHeaderSpecific(viewer,PETSC_VIEWER_CLASSID,2);
  PetscCheckSameComm(svd,1,viewer,2);

  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&isascii);CHKERRQ(ierr);
  if (isascii) {
    ierr = PetscObjectPrintClassNamePrefixType((PetscObject)svd,viewer);CHKERRQ(ierr);
    if (svd->ops->view) {
      ierr = PetscViewerASCIIPushTab(viewer);CHKERRQ(ierr);
      ierr = (*svd->ops->view)(svd,viewer);CHKERRQ(ierr);
      ierr = PetscViewerASCIIPopTab(viewer);CHKERRQ(ierr);
    }
    if (svd->problem_type) {
      switch (svd->problem_type) {
        case SVD_STANDARD:    type = "(standard) singular value problem"; break;
        case SVD_GENERALIZED: type = "generalized singular value problem"; break;
      }
    } else type = "not yet set";
    ierr = PetscViewerASCIIPrintf(viewer,"  problem type: %s\n",type);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  transpose mode: %s\n",svd->impltrans?"implicit":"explicit");CHKERRQ(ierr);
    if (svd->which == SVD_LARGEST) {
      ierr = PetscViewerASCIIPrintf(viewer,"  selected portion of the spectrum: largest\n");CHKERRQ(ierr);
    } else {
      ierr = PetscViewerASCIIPrintf(viewer,"  selected portion of the spectrum: smallest\n");CHKERRQ(ierr);
    }
    ierr = PetscViewerASCIIPrintf(viewer,"  number of singular values (nsv): %" PetscInt_FMT "\n",svd->nsv);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  number of column vectors (ncv): %" PetscInt_FMT "\n",svd->ncv);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  maximum dimension of projected problem (mpd): %" PetscInt_FMT "\n",svd->mpd);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  maximum number of iterations: %" PetscInt_FMT "\n",svd->max_it);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  tolerance: %g\n",(double)svd->tol);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"  convergence test: ");CHKERRQ(ierr);
    ierr = PetscViewerASCIIUseTabs(viewer,PETSC_FALSE);CHKERRQ(ierr);
    switch (svd->conv) {
    case SVD_CONV_ABS:
      ierr = PetscViewerASCIIPrintf(viewer,"absolute\n");CHKERRQ(ierr);break;
    case SVD_CONV_REL:
      ierr = PetscViewerASCIIPrintf(viewer,"relative to the singular value\n");CHKERRQ(ierr);break;
    case SVD_CONV_NORM:
      ierr = PetscViewerASCIIPrintf(viewer,"relative to the matrix norms\n");CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(viewer,"  computed matrix norms: norm(A)=%g",(double)svd->nrma);CHKERRQ(ierr);
      if (svd->isgeneralized) {
        ierr = PetscViewerASCIIPrintf(viewer,", norm(B)=%g",(double)svd->nrmb);CHKERRQ(ierr);
      }
      ierr = PetscViewerASCIIPrintf(viewer,"\n");CHKERRQ(ierr);
      break;
    case SVD_CONV_MAXIT:
      ierr = PetscViewerASCIIPrintf(viewer,"maximum number of iterations\n");CHKERRQ(ierr);break;
    case SVD_CONV_USER:
      ierr = PetscViewerASCIIPrintf(viewer,"user-defined\n");CHKERRQ(ierr);break;
    }
    ierr = PetscViewerASCIIUseTabs(viewer,PETSC_TRUE);CHKERRQ(ierr);
    if (svd->nini) {
      ierr = PetscViewerASCIIPrintf(viewer,"  dimension of user-provided initial space: %" PetscInt_FMT "\n",PetscAbs(svd->nini));CHKERRQ(ierr);
    }
    if (svd->ninil) {
      ierr = PetscViewerASCIIPrintf(viewer,"  dimension of user-provided initial left space: %" PetscInt_FMT "\n",PetscAbs(svd->ninil));CHKERRQ(ierr);
    }
  } else {
    if (svd->ops->view) {
      ierr = (*svd->ops->view)(svd,viewer);CHKERRQ(ierr);
    }
  }
  ierr = PetscObjectTypeCompareAny((PetscObject)svd,&isshell,SVDCROSS,SVDCYCLIC,SVDSCALAPACK,SVDELEMENTAL,SVDPRIMME,"");CHKERRQ(ierr);
  if (!isshell) {
    ierr = PetscViewerPushFormat(viewer,PETSC_VIEWER_ASCII_INFO);CHKERRQ(ierr);
    if (!svd->V) { ierr = SVDGetBV(svd,&svd->V,NULL);CHKERRQ(ierr); }
    ierr = BVView(svd->V,viewer);CHKERRQ(ierr);
    if (!svd->ds) { ierr = SVDGetDS(svd,&svd->ds);CHKERRQ(ierr); }
    ierr = DSView(svd->ds,viewer);CHKERRQ(ierr);
    ierr = PetscViewerPopFormat(viewer);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/*@C
   SVDViewFromOptions - View from options

   Collective on SVD

   Input Parameters:
+  svd  - the singular value solver context
.  obj  - optional object
-  name - command line option

   Level: intermediate

.seealso: SVDView(), SVDCreate()
@*/
PetscErrorCode SVDViewFromOptions(SVD svd,PetscObject obj,const char name[])
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(svd,SVD_CLASSID,1);
  ierr = PetscObjectViewFromOptions((PetscObject)svd,obj,name);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*@C
   SVDConvergedReasonView - Displays the reason an SVD solve converged or diverged.

   Collective on svd

   Input Parameters:
+  svd - the singular value solver context
-  viewer - the viewer to display the reason

   Options Database Keys:
.  -svd_converged_reason - print reason for convergence, and number of iterations

   Note:
   To change the format of the output call PetscViewerPushFormat(viewer,format) before
   this call. Use PETSC_VIEWER_DEFAULT for the default, use PETSC_VIEWER_FAILED to only
   display a reason if it fails. The latter can be set in the command line with
   -svd_converged_reason ::failed

   Level: intermediate

.seealso: SVDSetTolerances(), SVDGetIterationNumber(), SVDConvergedReasonViewFromOptions()
@*/
PetscErrorCode SVDConvergedReasonView(SVD svd,PetscViewer viewer)
{
  PetscErrorCode    ierr;
  PetscBool         isAscii;
  PetscViewerFormat format;

  PetscFunctionBegin;
  if (!viewer) viewer = PETSC_VIEWER_STDOUT_(PetscObjectComm((PetscObject)svd));
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&isAscii);CHKERRQ(ierr);
  if (isAscii) {
    ierr = PetscViewerGetFormat(viewer,&format);CHKERRQ(ierr);
    ierr = PetscViewerASCIIAddTab(viewer,((PetscObject)svd)->tablevel);CHKERRQ(ierr);
    if (svd->reason > 0 && format != PETSC_VIEWER_FAILED) {
      ierr = PetscViewerASCIIPrintf(viewer,"%s SVD solve converged (%" PetscInt_FMT " singular triplet%s) due to %s; iterations %" PetscInt_FMT "\n",((PetscObject)svd)->prefix?((PetscObject)svd)->prefix:"",svd->nconv,(svd->nconv>1)?"s":"",SVDConvergedReasons[svd->reason],svd->its);CHKERRQ(ierr);
    } else if (svd->reason <= 0) {
      ierr = PetscViewerASCIIPrintf(viewer,"%s SVD solve did not converge due to %s; iterations %" PetscInt_FMT "\n",((PetscObject)svd)->prefix?((PetscObject)svd)->prefix:"",SVDConvergedReasons[svd->reason],svd->its);CHKERRQ(ierr);
    }
    ierr = PetscViewerASCIISubtractTab(viewer,((PetscObject)svd)->tablevel);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/*@
   SVDConvergedReasonViewFromOptions - Processes command line options to determine if/how
   the SVD converged reason is to be viewed.

   Collective on svd

   Input Parameter:
.  svd - the singular value solver context

   Level: developer

.seealso: SVDConvergedReasonView()
@*/
PetscErrorCode SVDConvergedReasonViewFromOptions(SVD svd)
{
  PetscErrorCode    ierr;
  PetscViewer       viewer;
  PetscBool         flg;
  static PetscBool  incall = PETSC_FALSE;
  PetscViewerFormat format;

  PetscFunctionBegin;
  if (incall) PetscFunctionReturn(0);
  incall = PETSC_TRUE;
  ierr = PetscOptionsGetViewer(PetscObjectComm((PetscObject)svd),((PetscObject)svd)->options,((PetscObject)svd)->prefix,"-svd_converged_reason",&viewer,&format,&flg);CHKERRQ(ierr);
  if (flg) {
    ierr = PetscViewerPushFormat(viewer,format);CHKERRQ(ierr);
    ierr = SVDConvergedReasonView(svd,viewer);CHKERRQ(ierr);
    ierr = PetscViewerPopFormat(viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }
  incall = PETSC_FALSE;
  PetscFunctionReturn(0);
}

static PetscErrorCode SVDErrorView_ASCII(SVD svd,SVDErrorType etype,PetscViewer viewer)
{
  PetscReal      error,sigma;
  PetscInt       i,j;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (svd->nconv<svd->nsv) {
    ierr = PetscViewerASCIIPrintf(viewer," Problem: less than %" PetscInt_FMT " singular values converged\n\n",svd->nsv);CHKERRQ(ierr);
    PetscFunctionReturn(0);
  }
  for (i=0;i<svd->nsv;i++) {
    ierr = SVDComputeError(svd,i,etype,&error);CHKERRQ(ierr);
    if (error>=5.0*svd->tol) {
      ierr = PetscViewerASCIIPrintf(viewer," Problem: some of the first %" PetscInt_FMT " relative errors are higher than the tolerance\n\n",svd->nsv);CHKERRQ(ierr);
      PetscFunctionReturn(0);
    }
  }
  ierr = PetscViewerASCIIPrintf(viewer," All requested singular values computed up to the required tolerance:");CHKERRQ(ierr);
  for (i=0;i<=(svd->nsv-1)/8;i++) {
    ierr = PetscViewerASCIIPrintf(viewer,"\n     ");CHKERRQ(ierr);
    for (j=0;j<PetscMin(8,svd->nsv-8*i);j++) {
      ierr = SVDGetSingularTriplet(svd,8*i+j,&sigma,NULL,NULL);CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(viewer,"%.5f",(double)sigma);CHKERRQ(ierr);
      if (8*i+j+1<svd->nsv) { ierr = PetscViewerASCIIPrintf(viewer,", ");CHKERRQ(ierr); }
    }
  }
  ierr = PetscViewerASCIIPrintf(viewer,"\n\n");CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode SVDErrorView_DETAIL(SVD svd,SVDErrorType etype,PetscViewer viewer)
{
  PetscErrorCode ierr;
  PetscReal      error,sigma;
  PetscInt       i;
  char           ex[30],sep[]=" ---------------------- --------------------\n";

  PetscFunctionBegin;
  if (!svd->nconv) PetscFunctionReturn(0);
  switch (etype) {
    case SVD_ERROR_ABSOLUTE:
      ierr = PetscSNPrintf(ex,sizeof(ex)," absolute error");CHKERRQ(ierr);
      break;
    case SVD_ERROR_RELATIVE:
      ierr = PetscSNPrintf(ex,sizeof(ex)," relative error");CHKERRQ(ierr);
      break;
  }
  ierr = PetscViewerASCIIPrintf(viewer,"%s          sigma           %s\n%s",sep,ex,sep);CHKERRQ(ierr);
  for (i=0;i<svd->nconv;i++) {
    ierr = SVDGetSingularTriplet(svd,i,&sigma,NULL,NULL);CHKERRQ(ierr);
    ierr = SVDComputeError(svd,i,etype,&error);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"       % 6f          %12g\n",(double)sigma,(double)error);CHKERRQ(ierr);
  }
  ierr = PetscViewerASCIIPrintf(viewer,"%s",sep);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode SVDErrorView_MATLAB(SVD svd,SVDErrorType etype,PetscViewer viewer)
{
  PetscErrorCode ierr;
  PetscReal      error;
  PetscInt       i;
  const char     *name;

  PetscFunctionBegin;
  ierr = PetscObjectGetName((PetscObject)svd,&name);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"Error_%s = [\n",name);CHKERRQ(ierr);
  for (i=0;i<svd->nconv;i++) {
    ierr = SVDComputeError(svd,i,etype,&error);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,"%18.16e\n",(double)error);CHKERRQ(ierr);
  }
  ierr = PetscViewerASCIIPrintf(viewer,"];\n");CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*@C
   SVDErrorView - Displays the errors associated with the computed solution
   (as well as the singular values).

   Collective on svd

   Input Parameters:
+  svd    - the singular value solver context
.  etype  - error type
-  viewer - optional visualization context

   Options Database Key:
+  -svd_error_absolute - print absolute errors of each singular triplet
-  -svd_error_relative - print relative errors of each singular triplet

   Notes:
   By default, this function checks the error of all singular triplets and prints
   the singular values if all of them are below the requested tolerance.
   If the viewer has format=PETSC_VIEWER_ASCII_INFO_DETAIL then a table with
   singular values and corresponding errors is printed.

   Level: intermediate

.seealso: SVDSolve(), SVDValuesView(), SVDVectorsView()
@*/
PetscErrorCode SVDErrorView(SVD svd,SVDErrorType etype,PetscViewer viewer)
{
  PetscBool         isascii;
  PetscViewerFormat format;
  PetscErrorCode    ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(svd,SVD_CLASSID,1);
  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(PetscObjectComm((PetscObject)svd),&viewer);CHKERRQ(ierr);
  }
  PetscValidHeaderSpecific(viewer,PETSC_VIEWER_CLASSID,3);
  PetscCheckSameComm(svd,1,viewer,3);
  SVDCheckSolved(svd,1);
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&isascii);CHKERRQ(ierr);
  if (!isascii) PetscFunctionReturn(0);

  ierr = PetscViewerGetFormat(viewer,&format);CHKERRQ(ierr);
  switch (format) {
    case PETSC_VIEWER_DEFAULT:
    case PETSC_VIEWER_ASCII_INFO:
      ierr = SVDErrorView_ASCII(svd,etype,viewer);CHKERRQ(ierr);
      break;
    case PETSC_VIEWER_ASCII_INFO_DETAIL:
      ierr = SVDErrorView_DETAIL(svd,etype,viewer);CHKERRQ(ierr);
      break;
    case PETSC_VIEWER_ASCII_MATLAB:
      ierr = SVDErrorView_MATLAB(svd,etype,viewer);CHKERRQ(ierr);
      break;
    default:
      ierr = PetscInfo1(svd,"Unsupported viewer format %s\n",PetscViewerFormats[format]);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/*@
   SVDErrorViewFromOptions - Processes command line options to determine if/how
   the errors of the computed solution are to be viewed.

   Collective on svd

   Input Parameter:
.  svd - the singular value solver context

   Level: developer

.seealso: SVDErrorView()
@*/
PetscErrorCode SVDErrorViewFromOptions(SVD svd)
{
  PetscErrorCode    ierr;
  PetscViewer       viewer;
  PetscBool         flg;
  static PetscBool  incall = PETSC_FALSE;
  PetscViewerFormat format;

  PetscFunctionBegin;
  if (incall) PetscFunctionReturn(0);
  incall = PETSC_TRUE;
  ierr = PetscOptionsGetViewer(PetscObjectComm((PetscObject)svd),((PetscObject)svd)->options,((PetscObject)svd)->prefix,"-svd_error_absolute",&viewer,&format,&flg);CHKERRQ(ierr);
  if (flg) {
    ierr = PetscViewerPushFormat(viewer,format);CHKERRQ(ierr);
    ierr = SVDErrorView(svd,SVD_ERROR_ABSOLUTE,viewer);CHKERRQ(ierr);
    ierr = PetscViewerPopFormat(viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }
  ierr = PetscOptionsGetViewer(PetscObjectComm((PetscObject)svd),((PetscObject)svd)->options,((PetscObject)svd)->prefix,"-svd_error_relative",&viewer,&format,&flg);CHKERRQ(ierr);
  if (flg) {
    ierr = PetscViewerPushFormat(viewer,format);CHKERRQ(ierr);
    ierr = SVDErrorView(svd,SVD_ERROR_RELATIVE,viewer);CHKERRQ(ierr);
    ierr = PetscViewerPopFormat(viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }
  incall = PETSC_FALSE;
  PetscFunctionReturn(0);
}

static PetscErrorCode SVDValuesView_DRAW(SVD svd,PetscViewer viewer)
{
  PetscErrorCode ierr;
  PetscDraw      draw;
  PetscDrawSP    drawsp;
  PetscReal      re,im=0.0;
  PetscInt       i;

  PetscFunctionBegin;
  if (!svd->nconv) PetscFunctionReturn(0);
  ierr = PetscViewerDrawGetDraw(viewer,0,&draw);CHKERRQ(ierr);
  ierr = PetscDrawSetTitle(draw,"Computed singular values");CHKERRQ(ierr);
  ierr = PetscDrawSPCreate(draw,1,&drawsp);CHKERRQ(ierr);
  for (i=0;i<svd->nconv;i++) {
    re = svd->sigma[svd->perm[i]];
    ierr = PetscDrawSPAddPoint(drawsp,&re,&im);CHKERRQ(ierr);
  }
  ierr = PetscDrawSPDraw(drawsp,PETSC_TRUE);CHKERRQ(ierr);
  ierr = PetscDrawSPSave(drawsp);CHKERRQ(ierr);
  ierr = PetscDrawSPDestroy(&drawsp);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode SVDValuesView_BINARY(SVD svd,PetscViewer viewer)
{
  PetscInt       i,k;
  PetscReal      *sv;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscMalloc1(svd->nconv,&sv);CHKERRQ(ierr);
  for (i=0;i<svd->nconv;i++) {
    k = svd->perm[i];
    sv[i] = svd->sigma[k];
  }
  ierr = PetscViewerBinaryWrite(viewer,sv,svd->nconv,PETSC_REAL);CHKERRQ(ierr);
  ierr = PetscFree(sv);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#if defined(PETSC_HAVE_HDF5)
static PetscErrorCode SVDValuesView_HDF5(SVD svd,PetscViewer viewer)
{
  PetscErrorCode ierr;
  PetscInt       i,k,n,N;
  PetscMPIInt    rank;
  Vec            v;
  char           vname[30];
  const char     *ename;

  PetscFunctionBegin;
  ierr = MPI_Comm_rank(PetscObjectComm((PetscObject)svd),&rank);CHKERRMPI(ierr);
  N = svd->nconv;
  n = rank? 0: N;
  /* create a vector containing the singular values */
  ierr = VecCreateMPI(PetscObjectComm((PetscObject)svd),n,N,&v);CHKERRQ(ierr);
  ierr = PetscObjectGetName((PetscObject)svd,&ename);CHKERRQ(ierr);
  ierr = PetscSNPrintf(vname,sizeof(vname),"sigma_%s",ename);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)v,vname);CHKERRQ(ierr);
  if (!rank) {
    for (i=0;i<svd->nconv;i++) {
      k = svd->perm[i];
      ierr = VecSetValue(v,i,svd->sigma[k],INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(v);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(v);CHKERRQ(ierr);
  ierr = VecView(v,viewer);CHKERRQ(ierr);
  ierr = VecDestroy(&v);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}
#endif

static PetscErrorCode SVDValuesView_ASCII(SVD svd,PetscViewer viewer)
{
  PetscInt       i;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PetscViewerASCIIPrintf(viewer,"Singular values = \n");CHKERRQ(ierr);
  for (i=0;i<svd->nconv;i++) {
    ierr = PetscViewerASCIIPrintf(viewer,"   %.5f\n",(double)svd->sigma[svd->perm[i]]);CHKERRQ(ierr);
  }
  ierr = PetscViewerASCIIPrintf(viewer,"\n");CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode SVDValuesView_MATLAB(SVD svd,PetscViewer viewer)
{
  PetscErrorCode ierr;
  PetscInt       i;
  const char     *name;

  PetscFunctionBegin;
  ierr = PetscObjectGetName((PetscObject)svd,&name);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"Sigma_%s = [\n",name);CHKERRQ(ierr);
  for (i=0;i<svd->nconv;i++) {
    ierr = PetscViewerASCIIPrintf(viewer,"%18.16e\n",(double)svd->sigma[svd->perm[i]]);CHKERRQ(ierr);
  }
  ierr = PetscViewerASCIIPrintf(viewer,"];\n");CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*@C
   SVDValuesView - Displays the computed singular values in a viewer.

   Collective on svd

   Input Parameters:
+  svd    - the singular value solver context
-  viewer - the viewer

   Options Database Key:
.  -svd_view_values - print computed singular values

   Level: intermediate

.seealso: SVDSolve(), SVDVectorsView(), SVDErrorView()
@*/
PetscErrorCode SVDValuesView(SVD svd,PetscViewer viewer)
{
  PetscBool         isascii,isdraw,isbinary;
  PetscViewerFormat format;
  PetscErrorCode    ierr;
#if defined(PETSC_HAVE_HDF5)
  PetscBool         ishdf5;
#endif

  PetscFunctionBegin;
  PetscValidHeaderSpecific(svd,SVD_CLASSID,1);
  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(PetscObjectComm((PetscObject)svd),&viewer);CHKERRQ(ierr);
  }
  PetscValidHeaderSpecific(viewer,PETSC_VIEWER_CLASSID,2);
  PetscCheckSameComm(svd,1,viewer,2);
  SVDCheckSolved(svd,1);
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERDRAW,&isdraw);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERBINARY,&isbinary);CHKERRQ(ierr);
#if defined(PETSC_HAVE_HDF5)
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERHDF5,&ishdf5);CHKERRQ(ierr);
#endif
  ierr = PetscObjectTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&isascii);CHKERRQ(ierr);
  if (isdraw) {
    ierr = SVDValuesView_DRAW(svd,viewer);CHKERRQ(ierr);
  } else if (isbinary) {
    ierr = SVDValuesView_BINARY(svd,viewer);CHKERRQ(ierr);
#if defined(PETSC_HAVE_HDF5)
  } else if (ishdf5) {
    ierr = SVDValuesView_HDF5(svd,viewer);CHKERRQ(ierr);
#endif
  } else if (isascii) {
    ierr = PetscViewerGetFormat(viewer,&format);CHKERRQ(ierr);
    switch (format) {
      case PETSC_VIEWER_DEFAULT:
      case PETSC_VIEWER_ASCII_INFO:
      case PETSC_VIEWER_ASCII_INFO_DETAIL:
        ierr = SVDValuesView_ASCII(svd,viewer);CHKERRQ(ierr);
        break;
      case PETSC_VIEWER_ASCII_MATLAB:
        ierr = SVDValuesView_MATLAB(svd,viewer);CHKERRQ(ierr);
        break;
      default:
        ierr = PetscInfo1(svd,"Unsupported viewer format %s\n",PetscViewerFormats[format]);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}

/*@
   SVDValuesViewFromOptions - Processes command line options to determine if/how
   the computed singular values are to be viewed.

   Collective on svd

   Input Parameter:
.  svd - the singular value solver context

   Level: developer

.seealso: SVDValuesView()
@*/
PetscErrorCode SVDValuesViewFromOptions(SVD svd)
{
  PetscErrorCode    ierr;
  PetscViewer       viewer;
  PetscBool         flg;
  static PetscBool  incall = PETSC_FALSE;
  PetscViewerFormat format;

  PetscFunctionBegin;
  if (incall) PetscFunctionReturn(0);
  incall = PETSC_TRUE;
  ierr = PetscOptionsGetViewer(PetscObjectComm((PetscObject)svd),((PetscObject)svd)->options,((PetscObject)svd)->prefix,"-svd_view_values",&viewer,&format,&flg);CHKERRQ(ierr);
  if (flg) {
    ierr = PetscViewerPushFormat(viewer,format);CHKERRQ(ierr);
    ierr = SVDValuesView(svd,viewer);CHKERRQ(ierr);
    ierr = PetscViewerPopFormat(viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }
  incall = PETSC_FALSE;
  PetscFunctionReturn(0);
}

/*@C
   SVDVectorsView - Outputs computed singular vectors to a viewer.

   Collective on svd

   Input Parameters:
+  svd    - the singular value solver context
-  viewer - the viewer

   Options Database Keys:
.  -svd_view_vectors - output singular vectors

   Note:
   Right and left singular vectors are interleaved, that is, the vectors are
   output in the following order V0, U0, V1, U1, V2, U2, ...

   Level: intermediate

.seealso: SVDSolve(), SVDValuesView(), SVDErrorView()
@*/
PetscErrorCode SVDVectorsView(SVD svd,PetscViewer viewer)
{
  PetscErrorCode ierr;
  PetscInt       i,k;
  Vec            x;
  char           vname[30];
  const char     *ename;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(svd,SVD_CLASSID,1);
  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(PetscObjectComm((PetscObject)svd),&viewer);CHKERRQ(ierr);
  }
  PetscValidHeaderSpecific(viewer,PETSC_VIEWER_CLASSID,2);
  PetscCheckSameComm(svd,1,viewer,2);
  SVDCheckSolved(svd,1);
  if (svd->nconv) {
    ierr = PetscObjectGetName((PetscObject)svd,&ename);CHKERRQ(ierr);
    ierr = SVDComputeVectors(svd);CHKERRQ(ierr);
    for (i=0;i<svd->nconv;i++) {
      k = svd->perm[i];
      ierr = PetscSNPrintf(vname,sizeof(vname),"V%" PetscInt_FMT "_%s",i,ename);CHKERRQ(ierr);
      ierr = BVGetColumn(svd->V,k,&x);CHKERRQ(ierr);
      ierr = PetscObjectSetName((PetscObject)x,vname);CHKERRQ(ierr);
      ierr = VecView(x,viewer);CHKERRQ(ierr);
      ierr = BVRestoreColumn(svd->V,k,&x);CHKERRQ(ierr);
      ierr = PetscSNPrintf(vname,sizeof(vname),"U%" PetscInt_FMT "_%s",i,ename);CHKERRQ(ierr);
      ierr = BVGetColumn(svd->U,k,&x);CHKERRQ(ierr);
      ierr = PetscObjectSetName((PetscObject)x,vname);CHKERRQ(ierr);
      ierr = VecView(x,viewer);CHKERRQ(ierr);
      ierr = BVRestoreColumn(svd->U,k,&x);CHKERRQ(ierr);
    }
  }
  PetscFunctionReturn(0);
}

/*@
   SVDVectorsViewFromOptions - Processes command line options to determine if/how
   the computed singular vectors are to be viewed.

   Collective on svd

   Input Parameter:
.  svd - the singular value solver context

   Level: developer

.seealso: SVDVectorsView()
@*/
PetscErrorCode SVDVectorsViewFromOptions(SVD svd)
{
  PetscErrorCode    ierr;
  PetscViewer       viewer;
  PetscBool         flg = PETSC_FALSE;
  static PetscBool  incall = PETSC_FALSE;
  PetscViewerFormat format;

  PetscFunctionBegin;
  if (incall) PetscFunctionReturn(0);
  incall = PETSC_TRUE;
  ierr = PetscOptionsGetViewer(PetscObjectComm((PetscObject)svd),((PetscObject)svd)->options,((PetscObject)svd)->prefix,"-svd_view_vectors",&viewer,&format,&flg);CHKERRQ(ierr);
  if (flg) {
    ierr = PetscViewerPushFormat(viewer,format);CHKERRQ(ierr);
    ierr = SVDVectorsView(svd,viewer);CHKERRQ(ierr);
    ierr = PetscViewerPopFormat(viewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  }
  incall = PETSC_FALSE;
  PetscFunctionReturn(0);
}

