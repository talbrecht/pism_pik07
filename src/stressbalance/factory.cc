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

#include <memory>

#include "StressBalance.hh"
#include "ShallowStressBalance.hh"
#include "SSB_Modifier.hh"
#include "pism/regional/SSAFD_Regional.hh"
#include "pism/regional/SIAFD_Regional.hh"

namespace pism {
namespace stressbalance {

std::shared_ptr<StressBalance> create(const std::string &model,
                                      IceGrid::ConstPtr grid,
                                      bool regional) {
  ShallowStressBalance *sliding = NULL;

  if (model == "none" or model == "sia") {
    sliding = new ZeroSliding(grid);
  } else if (model == "prescribed_sliding" or model == "prescribed_sliding+sia") {
    sliding = new PrescribedSliding(grid);
  } else if (model == "ssa" or model == "ssa+sia") {
    if (regional) {
      sliding = new SSAFD_Regional(grid);
    } else {
      sliding = new SSAFD(grid);
    }
  } else {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                  "invalid stress balance model: %s", model.c_str());
  }

  SSB_Modifier *modifier = NULL;

  if (model == "none" or model == "ssa" or model == "prescribed_sliding") {
    modifier = new ConstantInColumn(grid);
  } else if (model == "prescribed_sliding+sia" or
             model == "ssa+sia" or
             model == "sia") {
    if (regional) {
      modifier = new SIAFD_Regional(grid);
    } else {
      modifier = new SIAFD(grid);
    }
  } else {
    throw RuntimeError::formatted(PISM_ERROR_LOCATION,
                                  "invalid stress balance model: %s", model.c_str());
  }

  return std::shared_ptr<StressBalance>(new StressBalance(grid, sliding, modifier));
}

} // end of namespace stressbalance
} // end of namespace pism
