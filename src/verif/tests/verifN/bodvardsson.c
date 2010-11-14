static const char help[] = 
"Solve Bodvardsson equations (Bueler interpretation) using SNES and\n"
"a dof=2 Vec and finite differences.\n\n";

#include "petscda.h"
#include "petscsnes.h"

#include "../exactTestN.h"

typedef struct {
  PetscScalar H, u;
} Node;


/* User-defined application context.  Used esp. by FormFunctionLocal().  */
typedef struct {
  DA          da;       /* 1d,dof=2 distributed array for soln and residual */
  DA          scalarda; /* 1d,dof=1 distributed array for parameters depending on x */
  PetscReal   secpera;
  PetscReal   n;
  PetscReal   rho;
  PetscReal   rhow;
  PetscReal   g;
  PetscReal   H0;      /* thickness at x=0, for Dirichlet condition on mass cont */
  PetscReal   xc;      /* location at which stress (Neumann) condition applied to SSA eqn */
  PetscReal   Txc;     /* vertically-integrated longitudinal stress at xc, for Neumann cond:
                            T = 2 H B |u_x|^{(1/n)-1} u_x  */
  PetscReal   epsilon;
  Vec         Huexact; /* exact thickness (Huexact[i][0]) and exact velocity
                          (Huexact[i][1]) on regular grid */
  Vec         M;       /* surface mass balance on regular grid */
  Vec         beta;    /* sliding coefficient on regular grid */
  Vec         B_stag;  /* ice hardness on staggered grid*/
} AppCtx;


#undef __FUNCT__
#define __FUNCT__ "FillExactSoln"
/*  Compute the exact thickness and velocity on the regular grid. */
static PetscErrorCode FillExactSoln(AppCtx *user)
{
  PetscErrorCode ierr;
  PetscInt       i,Mx,xs,xm;
  PetscScalar    dx, x, dum1, dum2, dum3, dum4;
  Node           *Hu;

  PetscFunctionBegin;
  ierr = DAGetInfo(user->da,PETSC_IGNORE,&Mx,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,
                   PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE);
  ierr = DAGetCorners(user->da,&xs,PETSC_NULL,PETSC_NULL,&xm,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);

  dx = user->xc / (PetscReal)(Mx-1);

  /* Compute regular grid exact soln and staggered-grid thickness over the
     locally-owned part of the grid */
  ierr = DAVecGetArray(user->da,user->Huexact,&Hu);CHKERRQ(ierr);
  for (i=xs; i<xs+xm; i++) {
    x = dx * (PetscReal)i;  /* = x_i = distance from dome */
    ierr = exactN(x, &(Hu[i].H), &dum1, &(Hu[i].u), &dum2, &dum3, &dum4); CHKERRQ(ierr);
  }
  ierr = DAVecRestoreArray(user->da,user->Huexact,&Hu);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "FillDistributedParams"
/*  Compute the values of the surface mass balance, ice hardness, and sliding coeff. */
static PetscErrorCode FillDistributedParams(AppCtx *user)
{
  PetscErrorCode ierr;
  PetscInt       i,Mx,xs,xm;
  PetscScalar    dx, x, dum1, dum2, dum3, dum4, dum5, *M, *Bstag, *beta;

  PetscFunctionBegin;
  ierr = DAGetInfo(user->scalarda,PETSC_IGNORE,&Mx,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,
                   PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE);
  ierr = DAGetCorners(user->scalarda,&xs,PETSC_NULL,PETSC_NULL,&xm,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);

  dx = user->xc / (PetscReal)(Mx-1);

  /* Compute regular grid exact soln and staggered-grid thickness over the
     locally-owned part of the grid */
  ierr = DAVecGetArray(user->scalarda,user->M,&M);CHKERRQ(ierr);
  ierr = DAVecGetArray(user->scalarda,user->B_stag,&Bstag);CHKERRQ(ierr);
  ierr = DAVecGetArray(user->scalarda,user->beta,&beta);CHKERRQ(ierr);
  for (i=xs; i<xs+xm; i++) {
    x = dx * (PetscReal)i;  /* = x_i = distance from dome; regular grid point */
    ierr = exactN(x, &dum1, &dum2, &dum3, &(M[i]), &dum4, &(beta[i])); CHKERRQ(ierr);
    x = x + (dx/2.0);       /* = x_{i+1/2}; staggered grid point */
    if (i < Mx-1) {
      ierr = exactN(x, &dum1, &dum2, &dum3, &dum4, &(Bstag[i]), &dum5); CHKERRQ(ierr);
    } else {
      Bstag[i] = -9999.9999;
    }
  }
  ierr = DAVecRestoreArray(user->scalarda,user->M,&M);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(user->scalarda,user->B_stag,&Bstag);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(user->scalarda,user->beta,&beta);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}


#undef __FUNCT__
#define __FUNCT__ "GetFSR"
/* define a power of the strain rate:   F  \approx  |u_x|^q u_x   */
static inline PetscScalar GetFSR(PetscScalar dx, PetscScalar eps, PetscScalar n, 
                                 PetscScalar ul, PetscScalar ur) {
  PetscScalar dudx = (ur - ul) / dx, 
              q    = (1.0 / n) - 1.0;
  return PetscPowScalar(dudx * dudx + eps * eps, q / 2.0) * dudx;
}


#undef __FUNCT__
#define __FUNCT__ "FormFunctionLocal"
static PetscErrorCode BodFunctionLocal(DALocalInfo *info, Node *Hu, Node *f, AppCtx *user)
{
  PetscErrorCode ierr;
  PetscReal      dx, rg;
  PetscScalar    *M, *Bstag, *beta,
                 duHl, ul, u, ur, dHdx, Fl, Fr, Tl, Tr;
  PetscInt       i, Mx = info->mx;
  Vec            locBstag;

  PetscFunctionBegin;

  /* we need stencil width on Bstag (but not for M, beta) */
  ierr = DAGetLocalVector(user->scalarda,&locBstag);CHKERRQ(ierr);  /* do NOT destroy it */
  ierr = DAGlobalToLocalBegin(user->scalarda,user->B_stag,INSERT_VALUES,locBstag); CHKERRQ(ierr);
  ierr = DAGlobalToLocalEnd(user->scalarda,user->B_stag,INSERT_VALUES,locBstag); CHKERRQ(ierr);

  dx = user->xc / ((PetscReal)Mx - 1.0);
  rg = user->rho * user->g;

  /* note:  we use Dirichlet conditions as values for neighbor, which tends to
            symmetrize FD- or MF-computed Jacobian */
  ierr = DAVecGetArray(user->scalarda,locBstag,&Bstag);CHKERRQ(ierr);
  ierr = DAVecGetArray(user->scalarda,user->M,&M);CHKERRQ(ierr);
  ierr = DAVecGetArray(user->scalarda,user->beta,&beta);CHKERRQ(ierr);
  for (i=info->xs; i<info->xs+info->xm; i++) {
  
    /* MASS CONT */
    if (i == 0) {
      /* residual at left-most point is Dirichlet cond. */
      f[0].H = Hu[0].H - user->H0;
    } else {
      duHl = Hu[i].u * Hu[i].H - ( (i == 1) ? 0.0 : Hu[i-1].u * Hu[i-1].H );
      f[i].H = dx * M[i] - duHl;  /* upwind; difference  (uH)  only to left */
    }

    /* SSA */
    if (i == 0) {
      /* residual at left-most point is Dirichlet cond. */
      f[0].u = Hu[0].u - 0.0;
    } else {
      /* residual: SSA eqn */
      /* consecutive values of u */
      ul = (i == 1) ? 0.0 : Hu[i-1].u;
      u  = Hu[i].u;
      ur = (i == Mx-1) ? -1.1e30 : Hu[i+1].u;
      /* surface slope */
      if (i == 1) { 
        dHdx  = (Hu[i+1].H - user->H0) / (2.0 * dx);
      } else if (i == Mx-1) {
        dHdx  = (Hu[i].H - Hu[i-1].H) / dx;
      } else { /* generic case */
        dHdx  = (Hu[i+1].H - Hu[i-1].H) / (2.0 * dx);
      }
      /* vertically-integrated longitudinal stress */
      Fl = GetFSR(dx,user->epsilon,user->n, ul,u);
      if (i == Mx-1) {
        Tl = 2.0 * (Hu[i-1].H + Hu[i].H) * Bstag[i-1] * Fl;
        Tr = 2.0 * user->Txc;
      } else {
        Fr = GetFSR(dx,user->epsilon,user->n, u,ur);
        Tl = (Hu[i-1].H + Hu[i].H) * Bstag[i-1] * Fl;
        Tr = (Hu[i].H + Hu[i+1].H) * Bstag[i] * Fr;        
      }

      f[i].u = (Fr - Fl) - dx * beta[i] * u - dx * rg * Hu[i].H * dHdx;

    }
  }
  ierr = DAVecRestoreArray(user->scalarda,locBstag,&Bstag);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(user->scalarda,user->M,&M);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(user->scalarda,user->beta,&beta);CHKERRQ(ierr);

  ierr = DARestoreLocalVector(user->scalarda,&locBstag);CHKERRQ(ierr);

  PetscFunctionReturn(0);
}


/* TODO:
static PetscErrorCode BodJacobianMatrixLocal(DALocalInfo*,Node*,Mat,AppCtx*); */


#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc,char **argv)
{
  PetscErrorCode         ierr;

  SNES                   snes;                 /* nonlinear solver */
  Vec                    Hu,r;                 /* solution, residual vectors */
  Mat                    J;                    /* Jacobian matrix */
  AppCtx                 user;                 /* user-defined work context */
  PetscInt               its,Mx;               /* iteration count, num of pts */
  PetscReal              errinf,               /* max norm of numerical error */
                         tmp1, tmp2, tmp3, tmp4, tmp5;
  PetscTruth             eps_set = PETSC_FALSE;
  SNESConvergedReason    reason;               /* Check convergence */

  PetscInitialize(&argc,&argv,(char *)0,help);

  ierr = PetscPrintf(PETSC_COMM_WORLD,
    "BODVARDSSON solves for thickness and velocity in 1D, steady ice stream\n"
    "  [run with -help for info and options]\n");CHKERRQ(ierr);

  user.n       = 3.0;          /* Glen flow law exponent */
  user.secpera = 31556926.0;
  user.rho     = 910.0;        /* kg m^-3 */
  user.rhow    = 1028.0;       /* kg m^-3 */
  user.g       = 9.81;         /* m s^-2 */
  
  /* ask Test N for its parameters, but only those we need to solve */
  ierr = params_exactN(&(user.H0), &tmp1, &(user.xc), &tmp2, &tmp3, &tmp4, &tmp5, 
                       &(user.Txc)); CHKERRQ(ierr);

  user.epsilon = (1.0 / user.secpera) / user.xc; /* regularize using strain rate
                                                    of 1/xc = ?.?e-6 per year */

  ierr = PetscOptionsBegin(PETSC_COMM_WORLD,NULL,
      "SSA solve options for bodvardsson",__FILE__);CHKERRQ(ierr);
  {
    ierr = PetscOptionsReal("-ssa_glen","Glen flow law exponent for SSA","",
                            user.n,&user.n,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsReal("-ssa_rho","ice density (kg m^-3) for SSA","",
                            user.rho,&user.rho,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsReal("-ssa_rhow","sea water density (kg m^-3) for SSA","",
                            user.rhow,&user.rhow,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsReal("-ssa_epsilon","regularization (a strain rate in units of 1/a)","",
                            user.epsilon * user.secpera,&user.epsilon,&eps_set);CHKERRQ(ierr);
    if (eps_set) {  user.epsilon *= 1.0 / user.secpera;  }
  }
  ierr = PetscOptionsEnd();CHKERRQ(ierr);

  /* check some user parameters for reasonableness ... there could be more ... */
  if (user.n < 1.0) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,
      "SSA solve options WARNING: glen exponent n=%g is out of range (require n>= 1)\n",
      user.n);CHKERRQ(ierr);
  }

  /* Create machinery for parallel grid management (DA), nonlinear solver (SNES), 
     and Vecs for fields (solution, RHS).  Note default Mx=20 is 
     number of grid points.  Also degrees of freedom = 2 (thickness and velocity
     at each point) and stencil radius = ghost width = 1.                                    */
  ierr = DACreate1d(PETSC_COMM_WORLD,DA_NONPERIODIC,-20,2,1,PETSC_NULL,&user.da);CHKERRQ(ierr);
  ierr = DASetUniformCoordinates(user.da,0.0,user.xc,
                                 PETSC_NULL,PETSC_NULL,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);

  /* another DA for scalar parameters */
  ierr = DACreate1d(PETSC_COMM_WORLD,DA_NONPERIODIC,-20,1,1,PETSC_NULL,&user.scalarda);CHKERRQ(ierr);
  ierr = DASetUniformCoordinates(user.scalarda,0.0,user.xc,
                                 PETSC_NULL,PETSC_NULL,PETSC_NULL,PETSC_NULL);CHKERRQ(ierr);

  /* Extract/allocate global vectors from DAs and duplicate for remaining same types */
  ierr = DACreateGlobalVector(user.da,&Hu);CHKERRQ(ierr);
  ierr = VecDuplicate(Hu,&r);CHKERRQ(ierr);
  ierr = VecDuplicate(Hu,&user.Huexact);CHKERRQ(ierr);

  ierr = DACreateGlobalVector(user.scalarda,&user.M);CHKERRQ(ierr);
  ierr = VecDuplicate(user.M,&user.B_stag);CHKERRQ(ierr);
  ierr = VecDuplicate(user.M,&user.beta);CHKERRQ(ierr);

  ierr = DASetLocalFunction(user.da,(DALocalFunction1)BodFunctionLocal);CHKERRQ(ierr);

  ierr = SNESCreate(PETSC_COMM_WORLD,&snes);CHKERRQ(ierr);

  ierr = SNESSetFunction(snes,r,SNESDAFormFunction,&user);CHKERRQ(ierr);

  /* setting up a matrix is only actually needed for -snes_fd case */
  ierr = DAGetMatrix(user.da,MATAIJ,&J);CHKERRQ(ierr);
  ierr = SNESSetJacobian(snes,J,J,SNESDAComputeJacobian,PETSC_NULL);CHKERRQ(ierr);

  ierr = SNESSetFromOptions(snes);CHKERRQ(ierr);

  /* the the Bodvardsson (1955) exact solution allows setting M(x), B(x), beta(x), T(xc) */
  ierr = FillDistributedParams(&user);CHKERRQ(ierr);

  ierr = PetscPrintf(PETSC_COMM_WORLD,"  using exact solution as initial guess\n");
             CHKERRQ(ierr);
  /* the exact thickness and exact ice velocity (user.uHexact) are known from Bodvardsson (1955) */
  ierr = FillExactSoln(&user);CHKERRQ(ierr);
  ierr = VecCopy(user.Huexact,Hu); CHKERRQ(ierr);
  
  /************ SOLVE NONLINEAR SYSTEM  ************/
  /* recall that RHS  r  is used internally by KSP, and is set by the SNES */
  ierr = SNESSolve(snes,PETSC_NULL,Hu);CHKERRQ(ierr);

  ierr = SNESGetIterationNumber(snes,&its);CHKERRQ(ierr);
  ierr = SNESGetConvergedReason(snes,&reason);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,
           "  %s Number of Newton iterations = %D\n",
           SNESConvergedReasons[reason],its);CHKERRQ(ierr);

  /* evaluate error relative to exact solution */
  ierr = VecAXPY(Hu,-1.0,user.Huexact);CHKERRQ(ierr); /* "y:=ax+y"  so   u := u - uexact */
  ierr = VecNorm(Hu,NORM_INFINITY,&errinf);CHKERRQ(ierr);
  ierr = DAGetInfo(user.da,PETSC_IGNORE,&Mx,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,
                   PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE);
  ierr = PetscPrintf(PETSC_COMM_WORLD,
           "  numerical errors in COMBINED: %.4e [UNITS??] maximum\n",
           errinf);CHKERRQ(ierr);
  
  ierr = VecDestroy(Hu);CHKERRQ(ierr);
  ierr = VecDestroy(r);CHKERRQ(ierr);
  ierr = VecDestroy(user.Huexact);CHKERRQ(ierr);
  ierr = VecDestroy(user.M);CHKERRQ(ierr);
  ierr = VecDestroy(user.B_stag);CHKERRQ(ierr);
  ierr = VecDestroy(user.beta);CHKERRQ(ierr);

  ierr = MatDestroy(J); CHKERRQ(ierr);

  ierr = SNESDestroy(snes);CHKERRQ(ierr);

  ierr = DADestroy(user.da);CHKERRQ(ierr);
  ierr = DADestroy(user.scalarda);CHKERRQ(ierr);

  ierr = PetscFinalize();CHKERRQ(ierr);
  return 0;
}


