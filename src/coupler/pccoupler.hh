// Copyright (C) 2009 Ed Bueler and Ricarda Winkelmann
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA


#ifndef __pccoupler_hh
#define __pccoupler_hh

#include <petsc.h>
#include "../base/grid.hh"


//! A virtual base class for coupling PISM to other climate components.
/*!
Methods and members here are common to all possible implementations and 
derived classes.
 */
class PISMClimateCoupler {

public:
  PISMClimateCoupler();
  virtual ~PISMClimateCoupler();

  virtual PetscErrorCode setGrid(IceGrid* g);

  // the implementations of this in the base class just terminates; to use,
  //   re-implement in the derived class
  virtual PetscErrorCode writeCouplingFieldsToFile(const char *filename);

protected:
  IceGrid* grid;
};


//! A basic derived class of PISMClimateCoupler for coupling PISM to an atmosphere model.
/*!
It is expected that a derived class of this will actually be used.
 */
class PISMAtmosphereCoupler : public PISMClimateCoupler {

public:
  PISMAtmosphereCoupler();
  virtual ~PISMAtmosphereCoupler(); // destroys IceModelVec2 below
  
  virtual PetscErrorCode getArrayForSurfMassFlux(PetscScalar** &a);
  virtual PetscErrorCode getArrayForSurfTemp(PetscScalar** &a);

  virtual PetscErrorCode writeCouplingFieldsToFile(const char *filename);

protected:
  IceModelVec2 vsurfmassflux;
  IceModelVec2 vsurftemp;
};


//! A derived class of PISMAtmosphereCoupler which provides a constant-in-time surface climate.
/*!
Reads surface temperature and mass balance from a NetCDF file.
 */
class PISMConstAtmosCoupler : public PISMAtmosphereCoupler {

public:
  PISMConstAtmosCoupler();

  PetscErrorCode readSurfaceTempFromFile(const char *filename);
  PetscErrorCode readSurfaceMassBalanceFromFile(const char *filename);
};


//! A derived class of PISMAtmosphereCoupler which provides a PDD to PISM.
/*!
The PDD here is the one already implemented in PISM.  That is, it is the one
from EISMINT-Greenland.  Thus it has various constants parameterizing the 
melt and refreeze processes.
 */
class PISMPDDCoupler : public PISMAtmosphereCoupler {

public:
  PISMPDDCoupler();
  ~PISMPDDCoupler();  // destroys PDD

  PetscErrorCode initPDD(/* parameters here */);
  PetscErrorCode writeCouplingFieldsToFile(const char *filename);
};


//! A draft derived class of PISMClimateCoupler for coupling PISM to an ocean model.
class PISMOceanCoupler : public PISMClimateCoupler {

public:
  PISMOceanCoupler();

  // a destructor will be needed to destroy pair of IceModelVec2 below
  // ~PISMOceanCoupler();

  // this procedure would initialize the IceModelVec2 below
  // PetscErrorCode init();
  
  // this procedure would write the two IceModelVec2 fields to 
  //   a NetCDF file; needed for debugging at least
  // PetscErrorCode writeCouplingFieldsToFile(const char *filename);

  // these two fields would store (map-plane, scalar) fields for ice shelf
  //   base temperature and ice shelf base mass flux
  // IceModelVec2 vShelfBTemp;
  // IceModelVec2 vShelfBFlux;

  // this procedure would put ice shelf base absolute temperature
  //   into vShelfBTemp;  units K;  the average over time interval [t,t+dt];
  //   shelf_base_elev is elevation of ice shelf base above sea level
  //   (thus always negative);
  // THIS PROCEDURE WOULD CALL THE OCEAN MODEL SOMEHOW
  // PetscErrorCode getShelfBasalTemp(PetscScalar t, PetscScalar dt, 
  //                   IceModelVec2 mask, IceModelVec2 shelf_base_elev);
                      
             
  // this procedure would put returns net ice shelf basal mass balance 
  //   into vShelfBFlux;  units of meters of ice per second;
  //   the average over time interval [t,t+dt];  positive means ice is added to shelf;
  //   shelf_base_elev as above
  // THIS PROCEDURE WOULD CALL THE OCEAN MODEL SOMEHOW
  // PetscErrorCode getShelfBasalMassFlux(PetscScalar t, PetscScalar dt,
  //                   IceModelVec2 mask, IceModelVec2 shelf_base_elev);

protected:
  // additional fields here if not needed by calling instance of IceModel (or derived)
};


#endif

