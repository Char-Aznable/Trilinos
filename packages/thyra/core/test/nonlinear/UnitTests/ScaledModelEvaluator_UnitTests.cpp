/*
// @HEADER
// ***********************************************************************
//
//    Thyra: Interfaces and Support for Abstract Numerical Algorithms
//                 Copyright (2004) Sandia Corporation
//
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Roscoe A. Bartlett (bartlettra@ornl.gov)
//
// ***********************************************************************
// @HEADER
*/

#include "Thyra_ScaledModelEvaluator.hpp"
#include "Thyra_Simple2DModelEvaluator.hpp"
#include "Thyra_SimpleDenseLinearOp.hpp"
#include "Thyra_DefaultSpmdVectorSpace.hpp"
#include "Thyra_ModelEvaluatorHelpers.hpp"
#include "Thyra_VectorStdOps.hpp"
#include "Thyra_TestingTools.hpp"
#include "Teuchos_UnitTestHarness.hpp"
#include "Teuchos_DefaultComm.hpp"


namespace Thyra {


using Teuchos::as;
using Teuchos::null;
using Teuchos::rcp;


TEUCHOS_UNIT_TEST_TEMPLATE_1_DECL( ScalarResidualModelEvaluator,
  basic, Scalar )
{

  typedef ScalarTraits<Scalar> ST;
  typedef typename ST::magnitudeType ScalarMag;
  //typedef ModelEvaluatorBase MEB; // unused

  RCP<ModelEvaluator<Scalar> > model =
    simple2DModelEvaluator<Scalar>();

  RCP<ScaledModelEvaluator<Scalar> > scaled_model =
    createNonconstScaledModelEvaluator<Scalar>(model);

  ModelEvaluatorBase::InArgs<Scalar> in_args = model->getNominalValues();

  RCP<VectorBase<Scalar> > x = createMember(model->get_x_space());
  V_S(x.ptr(), Teuchos::as<Scalar>(2.0));
  in_args.set_x(x);

  ModelEvaluatorBase::OutArgs<Scalar> out_args = model->createOutArgs();

  RCP<const VectorSpaceBase<Scalar> > f_space = model->get_f_space();

  RCP<VectorBase<Scalar> > f = createMember(f_space);
  RCP<VectorBase<Scalar> > f_scaled = createMember(f_space);

  RCP<LinearOpBase<Scalar> > W_op = model->create_W_op() ;
  RCP<LinearOpBase<Scalar> > W_scaled_op = model->create_W_op() ;

  RCP<VectorBase<Scalar> > scaling_diagonal = createMember(f_space);
  const Scalar val = as<Scalar>(2.0);
  V_S(scaling_diagonal.ptr(), val);

  scaled_model->set_f_scaling(scaling_diagonal);

  out_args.set_f(f);
  out_args.set_W_op(W_op);

  model->evalModel(in_args, out_args);

  out_args.set_f(f_scaled);
  out_args.set_W_op(W_scaled_op);

  scaled_model->evalModel(in_args, out_args);

  ScalarMag tol = as<ScalarMag>(10.0) * ST::eps();
  const Scalar two = as<Scalar>(2.0);

  {
    const ConstDetachedVectorView<Scalar> f_dv(f);
    const ConstDetachedVectorView<Scalar> f_scaled_dv(f_scaled);
    TEST_FLOATING_EQUALITY(two * f_dv(0), f_scaled_dv(0), tol);
    TEST_FLOATING_EQUALITY(two * f_dv(1), f_scaled_dv(1), tol);
  }

  const RCP<SimpleDenseLinearOp<Scalar> > W_sdlo =
    Teuchos::rcp_dynamic_cast<SimpleDenseLinearOp<Scalar> >(W_op, true);
  const RCP<MultiVectorBase<Scalar> > W_mv =
    W_sdlo->getNonconstMultiVector();

  const RCP<SimpleDenseLinearOp<Scalar> > W_scaled_sdlo =
    Teuchos::rcp_dynamic_cast<SimpleDenseLinearOp<Scalar> >(W_scaled_op, true);
  const RCP<MultiVectorBase<Scalar> > W_scaled_mv =
    W_scaled_sdlo->getNonconstMultiVector();

  {
    const ConstDetachedMultiVectorView<Scalar> W_dv(*W_mv);
    const ConstDetachedMultiVectorView<Scalar> W_scaled_dv(*W_scaled_mv);
    TEST_FLOATING_EQUALITY(two * W_dv(0,0), W_scaled_dv(0,0), tol);
    TEST_FLOATING_EQUALITY(two * W_dv(0,1), W_scaled_dv(0,1), tol);
    TEST_FLOATING_EQUALITY(two * W_dv(1,0), W_scaled_dv(1,0), tol);
    TEST_FLOATING_EQUALITY(two * W_dv(1,1), W_scaled_dv(1,1), tol);
  }

}

TEUCHOS_UNIT_TEST_TEMPLATE_1_INSTANT_REAL_SCALAR_TYPES(
  ScalarResidualModelEvaluator, basic )


} // namespace Thyra


