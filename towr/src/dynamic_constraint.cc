/******************************************************************************
Copyright (c) 2017, Alexander W. Winkler, ETH Zurich. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of ETH ZURICH nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ETH ZURICH BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************************************************************/

#include <towr/constraints/dynamic_constraint.h>

#include <towr/variables/variable_names.h>
#include <towr/variables/cartesian_dimensions.h>

namespace towr {

DynamicConstraint::DynamicConstraint (const DynamicModel::Ptr& m,
                                      const std::vector<double>& evaluation_times,
                                      const SplineHolder& spline_holder)
    :TimeDiscretizationConstraint(evaluation_times, "DynamicConstraint")
{
  model_ = m;

  // link with up-to-date spline variables
  base_linear_  = spline_holder.GetBaseLinear();
  base_angular_ = EulerConverter(spline_holder.GetBaseAngular());
  ee_forces_ = spline_holder.GetEEForce();
  ee_motion_ = spline_holder.GetEEMotion();

  SetRows(GetNumberOfNodes()*k6D);
}

int
DynamicConstraint::GetRow (int k, Dim6D dimension) const
{
  return k6D*k + dimension;
}

void
DynamicConstraint::UpdateConstraintAtInstance(double t, int k, VectorXd& g) const
{
  // acceleration the system should have, given by physics
  UpdateModel(t);
  Vector6d acc_model = model_->GetBaseAccelerationInWorld();

  // acceleration base polynomial has with current optimization variables
  Vector6d acc_parametrization = Vector6d::Zero();
  acc_parametrization.middleRows(AX, k3D) = base_angular_.GetAngularAccelerationInWorld(t);
  acc_parametrization.middleRows(LX, k3D) = base_linear_->GetPoint(t).a();

  for (auto dim : AllDim6D)
    g(GetRow(k,dim)) = acc_model(dim) - acc_parametrization(dim);
}

void
DynamicConstraint::UpdateBoundsAtInstance(double t, int k, VecBound& bounds) const
{
  double gravity = model_->g();

  for (auto dim : AllDim6D) {
    if (dim == LZ)
      bounds.at(GetRow(k,dim)) = ifopt::Bounds(gravity, gravity);
    else
      bounds.at(GetRow(k,dim)) = ifopt::BoundZero;
  }
}

void
DynamicConstraint::UpdateJacobianAtInstance(double t, int k, std::string var_set,
                                            Jacobian& jac) const
{
  UpdateModel(t);

  int n = jac.cols();
  Jacobian jac_model(k6D,n);
  Jacobian jac_parametrization(k6D,n);

  // sensitivity of dynamic constraint w.r.t base variables.
  if (var_set == id::base_lin_nodes) {
    Jacobian jac_base_lin_pos = base_linear_->GetJacobianWrtNodes(t,kPos);
    jac_model = model_->GetJacobianOfAccWrtBaseLin(jac_base_lin_pos);
    jac_parametrization.middleRows(LX, k3D) = base_linear_->GetJacobianWrtNodes(t,kAcc);
  }

  if (var_set == id::base_ang_nodes) {
    Jacobian jac_ang_vel_wrt_coeff = base_angular_.GetDerivOfAngVelWrtEulerNodes(t);
    jac_model = model_->GetJacobianOfAccWrtBaseAng(jac_ang_vel_wrt_coeff);
    jac_parametrization.middleRows(AX, k3D) = base_angular_.GetDerivOfAngAccWrtEulerNodes(t);
  }


  // sensitivity of dynamic constraint w.r.t. endeffector variables
  for (int ee=0; ee<model_->GetEECount(); ++ee) {

    if (var_set == id::EEForceNodes(ee)) {
      Jacobian jac_ee_force = ee_forces_.at(ee)->GetJacobianWrtNodes(t,kPos);
      jac_model = model_->GetJacobianofAccWrtForce(jac_ee_force, ee);
    }

    if (var_set == id::EEMotionNodes(ee)) {
      Jacobian jac_ee_pos = ee_motion_.at(ee)->GetJacobianWrtNodes(t,kPos);
      jac_model = model_->GetJacobianofAccWrtEEPos(jac_ee_pos, ee);
    }

    if (var_set == id::EESchedule(ee)) {
      Jacobian jac_f_dT = ee_forces_.at(ee)->GetJacobianOfPosWrtDurations(t);
      jac_model += model_->GetJacobianofAccWrtForce(jac_f_dT, ee);

      Jacobian jac_x_dT = ee_motion_.at(ee)->GetJacobianOfPosWrtDurations(t);
      jac_model +=  model_->GetJacobianofAccWrtEEPos(jac_x_dT, ee);
    }
  }

  jac.middleRows(GetRow(k,AX), k6D) = jac_model - jac_parametrization;
}

void
DynamicConstraint::UpdateModel (double t) const
{
  auto com_pos   = base_linear_->GetPoint(t).p();
  Eigen::Vector3d omega = base_angular_.GetAngularVelocityInWorld(t);

  int n_ee = model_->GetEECount();
  std::vector<Eigen::Vector3d> ee_pos;
  std::vector<Eigen::Vector3d> ee_force;
  for (int ee=0; ee<n_ee; ++ee) {
    ee_force.push_back(ee_forces_.at(ee)->GetPoint(t).p());
    ee_pos.push_back(ee_motion_.at(ee)->GetPoint(t).p());
  }

  model_->SetCurrent(com_pos, omega, ee_force, ee_pos);
}

} /* namespace towr */
