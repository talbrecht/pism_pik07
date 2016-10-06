// Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016 Constantine Khroulev
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 3 of the License, or (at your option) any later
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

#include "PISMConstantYieldStress.hh"

#include "base/util/pism_options.hh"
#include "base/util/PISMConfigInterface.hh"
#include "base/util/IceGrid.hh"
#include "base/util/MaxTimestep.hh"
#include "base/util/pism_utilities.hh"

namespace pism {

ConstantYieldStress::ConstantYieldStress(IceGrid::ConstPtr g)
  : YieldStress(g) {
  // empty
}

ConstantYieldStress::~ConstantYieldStress () {
  // empty
}

void ConstantYieldStress::init_impl() {
  m_log->message(2, "* Initializing the constant basal yield stress model...\n");

  InputOptions opts = process_input_options(m_grid->com);
  const double tauc = m_config->get_double("basal_yield_stress.constant.value");

  switch (opts.type) {
  case INIT_RESTART:
    m_basal_yield_stress.read(opts.filename, opts.record);
    break;
  case INIT_BOOTSTRAP:
    m_basal_yield_stress.regrid(opts.filename, OPTIONAL, tauc);
    break;
  case INIT_OTHER:
  default:
    // Set the constant value.
    m_basal_yield_stress.set(tauc);
  }

  regrid("ConstantYieldStress", m_basal_yield_stress);
}

MaxTimestep ConstantYieldStress::max_timestep_impl(double t) {
  (void) t;
  return MaxTimestep();
}

void ConstantYieldStress::define_model_state_impl(const PIO &output) const {
  m_basal_yield_stress.define(output);
}

void ConstantYieldStress::write_model_state_impl(const PIO &output) const {
  m_basal_yield_stress.write(output);
}

void ConstantYieldStress::update_impl() {
  // empty
}

} // end of namespace pism
