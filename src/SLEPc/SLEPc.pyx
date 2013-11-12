# -----------------------------------------------------------------------------

from petsc4py.PETSc import COMM_NULL
from petsc4py.PETSc import COMM_SELF
from petsc4py.PETSc import COMM_WORLD

# -----------------------------------------------------------------------------

from petsc4py.PETSc cimport MPI_Comm
from petsc4py.PETSc cimport PetscObject, PetscViewer
from petsc4py.PETSc cimport PetscVec, PetscMat
from petsc4py.PETSc cimport PetscKSP, PetscPC

from petsc4py.PETSc cimport Comm
from petsc4py.PETSc cimport Object, Viewer
from petsc4py.PETSc cimport Vec, Mat
from petsc4py.PETSc cimport KSP, PC

# -----------------------------------------------------------------------------

cdef extern from *:
    ctypedef char const_char "const char"

cdef inline object bytes2str(const_char p[]):
     if p == NULL:
         return None
     cdef bytes s = <char*>p
     if isinstance(s, str):
         return s
     else:
         return s.decode()

cdef inline object str2bytes(object s, const_char *p[]):
    if s is None:
        p[0] = NULL
        return None
    if not isinstance(s, bytes):
        s = s.encode()
    p[0] = <const_char*>(<char*>s)
    return s

cdef inline str S_(const_char p[]):
     if p == NULL: return None
     cdef bytes s = <char*>p
     return s if isinstance(s, str) else s.decode()

include "allocate.pxi"

# -----------------------------------------------------------------------------

# Vile hack for raising a exception and not contaminating traceback

cdef extern from *:
    enum: PETSC_ERR_PYTHON "(-1)"

cdef extern from *:
    void PyErr_SetObject(object, object)
    void *PyExc_RuntimeError

cdef object PetscError = <object>PyExc_RuntimeError
from petsc4py.PETSc import Error as PetscError

cdef inline int SETERR(int ierr) with gil:
    if (<void*>PetscError) != NULL:
        PyErr_SetObject(PetscError, <long>ierr)
    else:
        PyErr_SetObject(<object>PyExc_RuntimeError, <long>ierr)
    return ierr

cdef inline int CHKERR(int ierr) nogil except -1:
    if ierr == 0:
        return 0  # no error
    if ierr == PETSC_ERR_PYTHON:
        return -1 # Python error
    SETERR(ierr)
    return -1

# -----------------------------------------------------------------------------

cdef extern from "compat.h": pass
cdef extern from "custom.h": pass

cdef extern from *:
    ctypedef long   PetscInt
    ctypedef double PetscReal
    ctypedef double PetscScalar
    ctypedef PetscInt    const_PetscInt    "const PetscInt"
    ctypedef PetscReal   const_PetscReal   "const PetscReal"
    ctypedef PetscScalar const_PetscScalar "const PetscScalar"

cdef extern from "scalar.h":
    object      PyPetscScalar_FromPetscScalar(PetscScalar)
    PetscScalar PyPetscScalar_AsPetscScalar(object) except*

cdef inline object toInt(PetscInt value):
    return value
cdef inline PetscInt asInt(object value) except? -1:
    return value

cdef inline object toReal(PetscReal value):
    return value
cdef inline PetscReal asReal(object value) except? -1:
    return value

cdef inline object toScalar(PetscScalar value):
    return PyPetscScalar_FromPetscScalar(value)
cdef inline PetscScalar asScalar(object value) except*:
    return PyPetscScalar_AsPetscScalar(value)

# -----------------------------------------------------------------------------

include "slepcmpi.pxi"
include "slepcsys.pxi"
include "slepcst.pxi"
include "slepcip.pxi"
include "slepcds.pxi"
include "slepceps.pxi"
include "slepcsvd.pxi"
include "slepcqep.pxi"

# -----------------------------------------------------------------------------

__doc__ = \
"""
Scalable Library for Eigenvalue Problem Computations.
"""

DECIDE    = PETSC_DECIDE
DEFAULT   = PETSC_DEFAULT
DETERMINE = PETSC_DETERMINE

include "Sys.pyx"
include "ST.pyx"
include "IP.pyx"
include "DS.pyx"
include "EPS.pyx"
include "SVD.pyx"
include "QEP.pyx"

# -----------------------------------------------------------------------------

cdef extern from "Python.h":
    int Py_AtExit(void (*)())
    void PySys_WriteStderr(char*,...)

cdef extern from "stdio.h" nogil:
    ctypedef struct FILE
    FILE *stderr
    int fprintf(FILE *, char *, ...)

cdef int initialize(object args) except -1:
    if (<int>SlepcInitializeCalled): return 1
    # initialize SLEPC
    CHKERR( SlepcInitialize(NULL, NULL, NULL, NULL) )
    # register finalization function
    if Py_AtExit(finalize) < 0:
        PySys_WriteStderr("warning: could not register"
                          "SlepcFinalize() with Py_AtExit()", 0)
    return 1 # and we are done, enjoy !!

from petsc4py.PETSc cimport PyPetscType_Register

cdef extern from *:
    int SlepcInitializePackageAll()
    ctypedef int PetscClassId
    PetscClassId SLEPC_ST_CLASSID  "ST_CLASSID"
    PetscClassId SLEPC_IP_CLASSID  "IP_CLASSID"
    PetscClassId SLEPC_DS_CLASSID  "DS_CLASSID"
    PetscClassId SLEPC_EPS_CLASSID "EPS_CLASSID"
    PetscClassId SLEPC_SVD_CLASSID "SVD_CLASSID"
    PetscClassId SLEPC_QEP_CLASSID "QEP_CLASSID"

cdef int register(char path[]) except -1:
    # make sure all SLEPc packages are initialized
    CHKERR( SlepcInitializePackageAll() )
    # register Python types
    PyPetscType_Register(SLEPC_ST_CLASSID,  ST)
    PyPetscType_Register(SLEPC_IP_CLASSID,  IP)
    PyPetscType_Register(SLEPC_DS_CLASSID,  DS)
    PyPetscType_Register(SLEPC_EPS_CLASSID, EPS)
    PyPetscType_Register(SLEPC_SVD_CLASSID, SVD)
    PyPetscType_Register(SLEPC_QEP_CLASSID, QEP)
    return 0

cdef void finalize() nogil:
    # finalize SLEPc
    cdef int ierr = 0
    ierr = SlepcFinalize()
    if ierr != 0:
        fprintf(stderr, "SlepcFinalize() failed "
                "[error code: %d]\n", ierr)
    # and we are done, see you later !!

# -----------------------------------------------------------------------------

def _initialize(args=None):
    cdef int ready = initialize(args)
    if ready: register(NULL)

def _finalize():
    finalize()

# -----------------------------------------------------------------------------
