/* Copyright (C) 2017 PISM Authors
 *
 * This file is part of PISM.
 *
 * PISM is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 3 of the License, or (at your option) any later
 * version.
 *
 * PISM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PISM; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <petscsys.h>           // PETSC_COMM_WORLD

#include "pism/util/petscwrappers/PetscInitializer.hh"
#include "pism/util/error_handling.hh"
#include "pism/util/Context.hh"
#include "pism/util/ConfigInterface.hh"
#include "pism/util/Logger.hh"
#include "pism/util/pism_utilities.hh"

#include "GPBLD.hh"
#include "GPBLD3.hh"


using namespace pism;

// Stolen from PETSc.
int BlastCache(void)
{
  int         i,ierr,n = 1000000;
  PetscScalar *x,*y,*z,*a,*b;

  ierr = PetscMalloc1(5*n,&x);CHKERRQ(ierr);
  y    = x + n;
  z    = y + n;
  a    = z + n;
  b    = a + n;

  for (i=0; i<n; i++) {
    a[i] = (PetscScalar) i;
    y[i] = (PetscScalar) i;
    z[i] = (PetscScalar) i;
    b[i] = (PetscScalar) i;
    x[i] = (PetscScalar) i;
  }

  for (i=0; i<n; i++) a[i] = 3.0*x[i] + 2.0*y[i] + 3.3*z[i] - 25.*b[i];
  for (i=0; i<n; i++) b[i] = 3.0*x[i] + 2.0*y[i] + 3.3*a[i] - 25.*b[i];
  for (i=0; i<n; i++) z[i] = 3.0*x[i] + 2.0*y[i] + 3.3*a[i] - 25.*b[i];
  ierr = PetscFree(x);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

int main(int argc, char **argv) {

  MPI_Comm com = MPI_COMM_WORLD;
  petsc::Initializer petsc(argc, argv, "GPBLD3 speed test");

  com = PETSC_COMM_WORLD;

  try {
    Context::Ptr ctx = context_from_options(com, "gpbld_speed_test");
    Logger::Ptr log = ctx->log();
    EnthalpyConverter::Ptr EC = ctx->enthalpy_converter();
    Config::ConstPtr config = ctx->config();

    rheology::GPBLD gpbld("stress_balance.sia.", *config, EC);
    rheology::GPBLD3 gpbld3("stress_balance.sia.", *config, EC);

    // "column" size
    int N = 100 * 100 * 100;
    // number of tries
    int M = 100;

    log->message(1,
                 "Comparing GPBLD and GPBLD3: %d repetitions, column size %d...\n",
                 M, N);

    std::vector<double> stress(N), E(N), P(N), grain_size(N), result(N), result3(N);

    // initialize
    const double
      omega = 0.01,             // water fraction
      H     = 1000.0,           // slab thickness
      dz    = H / (N - 1);      // grid spacing

    for (int k = 0; k < N; ++k) {
      const double
        z     = k * dz,
        depth = H - z,
        p = EC->pressure(depth),
        T = EC->melting_temperature(p);

      E[k]          = EC->enthalpy(T, omega, p);
      P[k]          = p;
      stress[k]     = p;
      grain_size[k] = config->get_double("constants.ice.grain_size");
    }

    double times[2] = {0.0, 0.0};
    for (int k = 0; k < M; ++k) {
      {
        BlastCache();
        double start_time = get_time();
        gpbld.flow_n(&stress[0], &E[0], &P[0], &grain_size[0], N, &result[0]);
        double end_time = get_time();
        times[0] += (end_time - start_time);
      }
      {
        BlastCache();
        double start_time = get_time();
        gpbld3.flow_n(&stress[0], &E[0], &P[0], &grain_size[0], N, &result3[0]);
        double end_time = get_time();
        times[1] += (end_time - start_time);
      }
    }

    // check that they produce almost identical results
    for (int j = 0; j < N; ++j) {
      if(fabs(result[j] - result3[j]) > 1e-15) {
        throw RuntimeError::formatted(PISM_ERROR_LOCATION, "results at j are different");
      }
    }

    log->message(1,
                 "GPBLD:  %f seconds.\n"
                 "GPBLD3: %f seconds.\n"
                 "Ratio:  %f\n",
                 times[0], times[1], times[0] / times[1]);

  }
  catch (...) {
    handle_fatal_errors(com);
    return 1;
  }

  return 0;
}