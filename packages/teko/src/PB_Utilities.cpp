#include "PB_Utilities.hpp"

// Thyra includes
#include "Thyra_MultiVectorStdOps.hpp"
#include "Thyra_ZeroLinearOpBase.hpp"
#include "Thyra_DefaultDiagonalLinearOp.hpp"
#include "Thyra_DefaultAddedLinearOp.hpp"
#include "Thyra_EpetraExtDiagScaledMatProdTransformer.hpp"
#include "Thyra_EpetraExtDiagScalingTransformer.hpp"
#include "Thyra_EpetraExtAddTransformer.hpp"
#include "Thyra_DefaultScaledAdjointLinearOp.hpp"
#include "Thyra_DefaultMultipliedLinearOp.hpp"
#include "Thyra_DefaultZeroLinearOp.hpp"
#include "Thyra_DefaultProductMultiVector.hpp"
#include "Thyra_DefaultProductVectorSpace.hpp"
#include "Thyra_MultiVectorStdOps.hpp"
#include "Thyra_get_Epetra_Operator.hpp"
#include "Thyra_EpetraThyraWrappers.hpp"

// Teuchos includes
#include "Teuchos_Array.hpp"

// Epetra includes
#include "Epetra_Vector.h"
#include "Epetra_Map.h"

// Anasazi includes
#include "AnasaziBasicEigenproblem.hpp"
#include "AnasaziThyraAdapter.hpp"
#include "AnasaziBlockKrylovSchurSolMgr.hpp"
#include "AnasaziBlockKrylovSchur.hpp"
#include "AnasaziStatusTestMaxIters.hpp"

// PB includes
#include "Epetra/PB_EpetraHelpers.hpp"

#include <cmath>

namespace PB {

using Teuchos::rcp;
using Teuchos::rcp_dynamic_cast;
using Teuchos::RCP;

// distance function...not parallel...entirely internal to this cpp file
inline double dist(int dim,double * coords,int row,int col)
{
   double value = 0.0;
   for(int i=0;i<dim;i++)
      value += std::pow(coords[dim*row+i]-coords[dim*col+i],2.0);

   // the distance between the two
   return std::sqrt(value);
}

// distance function...not parallel...entirely internal to this cpp file
inline double dist(double * x,double * y,double * z,int stride,int row,int col)
{
   double value = 0.0;
   if(x!=0) value += std::pow(x[stride*row]-x[stride*col],2.0);
   if(y!=0) value += std::pow(y[stride*row]-y[stride*col],2.0);
   if(z!=0) value += std::pow(z[stride*row]-z[stride*col],2.0);

   // the distance between the two
   return std::sqrt(value);
}

/** \brief Build a graph Laplacian stenciled on a Epetra_CrsMatrix.
  *
  * This function builds a graph Laplacian given a (locally complete)
  * vector of coordinates and a stencil Epetra_CrsMatrix (could this be
  * a graph of Epetra_RowMatrix instead?). The resulting matrix will have
  * the negative of the inverse distance on off diagonals. And the sum
  * of the positive inverse distance of the off diagonals on the diagonal.
  * If there are no off diagonal entries in the stencil, the diagonal is
  * set to 0.
  *
  * \param[in]     dim     Number of physical dimensions (2D or 3D?).
  * \param[in]     coords  A vector containing the coordinates, with the <code>i</code>-th
  *                        coordinate beginning at <code>coords[i*dim]</code>.
  * \param[in]     stencil The stencil matrix used to describe the connectivity
  *                        of the graph Laplacian matrix.
  *
  * \returns The graph Laplacian matrix to be filled according to the <code>stencil</code> matrix.
  */
RCP<Epetra_CrsMatrix> buildGraphLaplacian(int dim,double * coords,const Epetra_CrsMatrix & stencil)
{
   // allocate a new matrix with storage for the laplacian...in case of diagonals add one extra storage
   RCP<Epetra_CrsMatrix> gl = rcp(new Epetra_CrsMatrix(Copy,stencil.RowMap(),stencil.ColMap(),
                                                       stencil.MaxNumEntries()+1),true);

   // allocate an additional value for the diagonal, if neccessary
   std::vector<double> rowData(stencil.GlobalMaxNumEntries()+1);
   std::vector<int> rowInd(stencil.GlobalMaxNumEntries()+1);

   // loop over all the rows
   for(int j=0;j<gl->NumMyRows();j++) {
      int row = gl->GRID(j);
      double diagValue = 0.0;
      int diagInd = -1;
      int rowSz = 0;

      // extract a copy of this row...put it in rowData, rowIndicies
      stencil.ExtractGlobalRowCopy(row,stencil.MaxNumEntries(),rowSz,&rowData[0],&rowInd[0]);
 
      // loop over elements of row
      for(int i=0;i<rowSz;i++) {
         int col = rowInd[i];

         // is this a 0 entry masquerading as some thing else?
         double value = rowData[i];
         if(value==0) continue;

         // for nondiagonal entries
         if(row!=col) {
            double d = dist(dim,coords,row,col);
            rowData[i] = -1.0/d;
            diagValue += rowData[i];
         }
         else 
            diagInd = i;
      }
    
      // handle diagonal entry
      if(diagInd<0) { // diagonal not in row
         rowData[rowSz] = -diagValue;
         rowInd[rowSz] = row;
         rowSz++;
      }
      else { // diagonal in row
         rowData[diagInd] = -diagValue;
         rowInd[diagInd] = row;
      }

      // insert row data into graph Laplacian matrix
      TEST_FOR_EXCEPT(gl->InsertGlobalValues(row,rowSz,&rowData[0],&rowInd[0]));
   }

   gl->FillComplete();

   return gl;
}

/** \brief Build a graph Laplacian stenciled on a Epetra_CrsMatrix.
  *
  * This function builds a graph Laplacian given a (locally complete)
  * vector of coordinates and a stencil Epetra_CrsMatrix (could this be
  * a graph of Epetra_RowMatrix instead?). The resulting matrix will have
  * the negative of the inverse distance on off diagonals. And the sum
  * of the positive inverse distance of the off diagonals on the diagonal.
  * If there are no off diagonal entries in the stencil, the diagonal is
  * set to 0.
  *
  * \param[in]     x       A vector containing the x-coordinates, with the <code>i</code>-th
  *                        coordinate beginning at <code>coords[i*stride]</code>.
  * \param[in]     y       A vector containing the y-coordinates, with the <code>i</code>-th
  *                        coordinate beginning at <code>coords[i*stride]</code>.
  * \param[in]     z       A vector containing the z-coordinates, with the <code>i</code>-th
  *                        coordinate beginning at <code>coords[i*stride]</code>.
  * \param[in]     stride  Stride between entries in the (x,y,z) coordinate array
  * \param[in]     stencil The stencil matrix used to describe the connectivity
  *                        of the graph Laplacian matrix.
  *
  * \returns The graph Laplacian matrix to be filled according to the <code>stencil</code> matrix.
  */
RCP<Epetra_CrsMatrix> buildGraphLaplacian(double * x,double * y,double * z,int stride,const Epetra_CrsMatrix & stencil)
{
   // allocate a new matrix with storage for the laplacian...in case of diagonals add one extra storage
   RCP<Epetra_CrsMatrix> gl = rcp(new Epetra_CrsMatrix(Copy,stencil.RowMap(),stencil.ColMap(),
                                                       stencil.MaxNumEntries()+1),true);

   // allocate an additional value for the diagonal, if neccessary
   std::vector<double> rowData(stencil.GlobalMaxNumEntries()+1);
   std::vector<int> rowInd(stencil.GlobalMaxNumEntries()+1);

   // loop over all the rows
   for(int j=0;j<gl->NumMyRows();j++) {
      int row = gl->GRID(j);
      double diagValue = 0.0;
      int diagInd = -1;
      int rowSz = 0;

      // extract a copy of this row...put it in rowData, rowIndicies
      stencil.ExtractGlobalRowCopy(row,stencil.MaxNumEntries(),rowSz,&rowData[0],&rowInd[0]);
 
      // loop over elements of row
      for(int i=0;i<rowSz;i++) {
         int col = rowInd[i];

         // is this a 0 entry masquerading as some thing else?
         double value = rowData[i];
         if(value==0) continue;

         // for nondiagonal entries
         if(row!=col) {
            double d = dist(x,y,z,stride,row,col);
            rowData[i] = -1.0/d;
            diagValue += rowData[i];
         }
         else 
            diagInd = i;
      }
    
      // handle diagonal entry
      if(diagInd<0) { // diagonal not in row
         rowData[rowSz] = -diagValue;
         rowInd[rowSz] = row;
         rowSz++;
      }
      else { // diagonal in row
         rowData[diagInd] = -diagValue;
         rowInd[diagInd] = row;
      }

      // insert row data into graph Laplacian matrix
      TEST_FOR_EXCEPT(gl->InsertGlobalValues(row,rowSz,&rowData[0],&rowInd[0]));
   }

   gl->FillComplete();

   return gl;
}

/** \brief Apply a linear operator to a multivector (think of this as a matrix
  *        vector multiply).
  *
  * Apply a linear operator to a multivector. This also permits arbitrary scaling
  * and addition of the result. This function gives
  *     
  *    \f$ y = \alpha A x + \beta y \f$
  *
  * \param[in]     A
  * \param[in]     x
  * \param[in,out] y
  * \param[in]     \alpha
  * \param[in]     \beta
  *
  */
void applyOp(const LinearOp & A,const MultiVector & x,MultiVector & y,double alpha,double beta)
{
   Thyra::apply(*A,Thyra::NONCONJ_ELE,*x,&*y,alpha,beta);
}

/** \brief Update the <code>y</code> vector so that \f$y = \alpha x+\beta y\f$
  */
void update(double alpha,const MultiVector & x,double beta,MultiVector & y)
{
   Teuchos::Array<double> scale;
   Teuchos::Array<Teuchos::Ptr<const Thyra::MultiVectorBase<double> > >  vec;

   // build arrays needed for linear combo
   scale.push_back(alpha);
   vec.push_back(x.ptr());

   // compute linear combination
   Thyra::linear_combination<double>(scale,vec,beta,y.ptr());
}

//! Get the strictly upper triangular portion of the matrix
BlockedLinearOp getUpperTriBlocks(const BlockedLinearOp & blo)
{
   int rows = blockRowCount(blo);

   TEUCHOS_ASSERT(rows==blockColCount(blo));

   RCP<const Thyra::ProductVectorSpaceBase<double> > range = blo->productRange();
   RCP<const Thyra::ProductVectorSpaceBase<double> > domain = blo->productDomain();

   // allocate new operator
   BlockedLinearOp upper = createBlockedOp();
 
   // build new operator 
   upper->beginBlockFill(rows,rows);

   for(int i=0;i<rows;i++) {
      // put zero operators on the diagonal
      // this gurantees the vector space of
      // the new operator are fully defined
      RCP<const Thyra::LinearOpBase<double> > zed 
            = Thyra::zero<double>(range->getBlock(i),domain->getBlock(i));
      upper->setBlock(i,i,zed);

      for(int j=i+1;j<rows;j++) {
         // get block i,j
         LinearOp uij = blo->getBlock(i,j);

         // stuff it in U
         if(uij!=Teuchos::null)
            upper->setBlock(i,j,uij);
      }
   }
   upper->endBlockFill();

   return upper;
}

//! Get the strictly lower triangular portion of the matrix
BlockedLinearOp getLowerTriBlocks(const BlockedLinearOp & blo)
{
   int rows = blockRowCount(blo);

   TEUCHOS_ASSERT(rows==blockColCount(blo));

   RCP<const Thyra::ProductVectorSpaceBase<double> > range = blo->productRange();
   RCP<const Thyra::ProductVectorSpaceBase<double> > domain = blo->productDomain();

   // allocate new operator
   BlockedLinearOp lower = createBlockedOp();
 
   // build new operator 
   lower->beginBlockFill(rows,rows);

   for(int i=0;i<rows;i++) {
      // put zero operators on the diagonal
      // this gurantees the vector space of
      // the new operator are fully defined
      RCP<const Thyra::LinearOpBase<double> > zed 
            = Thyra::zero<double>(range->getBlock(i),domain->getBlock(i));
      lower->setBlock(i,i,zed);

      for(int j=0;j<i;j++) {
         // get block i,j
         LinearOp uij = blo->getBlock(i,j);

         // stuff it in U
         if(uij!=Teuchos::null)
            lower->setBlock(i,j,uij);
      }
   }
   lower->endBlockFill();

   return lower;
}

/** \brief Build a zero operator mimicing the block structure
  *        of the passed in matrix.
  *
  * Build a zero operator mimicing the block structure
  * of the passed in matrix. Currently this function assumes
  * that the operator is "block" square. Also, this function
  * calls <code>beginBlockFill</code> but does not call
  * <code>endBlockFill</code>.  This is so that the user
  * can fill the matrix as they wish once created.
  *
  * \param[in] blo Blocked operator with desired structure.
  *
  * \returns A zero operator with the same block structure as
  *          the argument <code>blo</code>.
  *
  * \notes The caller is responsible for calling 
  *        <code>endBlockFill</code> on the returned blocked
  *        operator.
  */ 
BlockedLinearOp zeroBlockedOp(const BlockedLinearOp & blo)
{
   int rows = blockRowCount(blo);

   TEUCHOS_ASSERT(rows==blockColCount(blo)); // assert that matrix is square

   RCP<const Thyra::ProductVectorSpaceBase<double> > range = blo->productRange();
   RCP<const Thyra::ProductVectorSpaceBase<double> > domain = blo->productDomain();

   // allocate new operator
   BlockedLinearOp zeroOp = createBlockedOp();
 
   // build new operator 
   zeroOp->beginBlockFill(rows,rows);

   for(int i=0;i<rows;i++) {
      // put zero operators on the diagonal
      // this gurantees the vector space of
      // the new operator are fully defined
      RCP<const Thyra::LinearOpBase<double> > zed 
            = Thyra::zero<double>(range->getBlock(i),domain->getBlock(i));
      zeroOp->setBlock(i,i,zed);
   }

   return zeroOp;
}

//! Figure out if this operator is the zero operator (or null!)
bool isZeroOp(const LinearOp op)
{
   // if operator is null...then its zero!
   if(op==Teuchos::null) return true;

   // try to cast it to a zero linear operator
   const LinearOp test = rcp_dynamic_cast<const Thyra::ZeroLinearOpBase<double> >(op);

   // if it works...then its zero...otherwise its null
   return test!=Teuchos::null;
}

/** \brief Compute absolute row sum matrix.
  *
  * Compute the absolute row sum matrix. That is
  * a diagonal operator composed of the absolute value of the
  * row sum.
  *
  * \returns A diagonal operator.
  */
ModifiableLinearOp getAbsRowSumMatrix(const LinearOp & op)
{
   RCP<const Epetra_CrsMatrix> eCrsOp;

   try {
      // get Epetra_Operator
      RCP<const Epetra_Operator> eOp = Thyra::get_Epetra_Operator(*op);

      // cast it to a CrsMatrix
      eCrsOp = rcp_dynamic_cast<const Epetra_CrsMatrix>(eOp,true);
   }
   catch (std::exception & e) {
      RCP<Teuchos::FancyOStream> out = Teuchos::VerboseObjectBase::getDefaultOStream();

      *out << "PB: getAbsRowSumMatrix requires an Epetra_CrsMatrix\n";
      *out << "    Could not extract an Epetra_Operator from a \"" << op->description() << std::endl;
      *out << "           OR\n";
      *out << "    Could not cast an Epetra_Operator to a Epetra_CrsMatrix\n";
      *out << std::endl;
      *out << "*** THROWN EXCEPTION ***\n";
      *out << e.what() << std::endl;
      *out << "************************\n";
      
      throw e;
   }

   // extract diagonal
   const RCP<Epetra_Vector> ptrDiag = rcp(new Epetra_Vector(eCrsOp->RowMap()));
   Epetra_Vector & diag = *ptrDiag;
   
   // compute absolute value row sum
   diag.PutScalar(0.0);
   for(int i=0;i<eCrsOp->NumMyRows();i++) {
      double * values = 0;
      int numEntries;
      eCrsOp->ExtractMyRowView(i,numEntries,values);

      // build abs value row sum
      for(int j=0;j<numEntries;j++)
         diag[i] += std::abs(values[j]);
   }

   // build Thyra diagonal operator
   return PB::Epetra::thyraDiagOp(ptrDiag,eCrsOp->RowMap(),"absRowSum( " + op->getObjectLabel() + " )");
}

/** \brief Compute inverse of the absolute row sum matrix.
  *
  * Compute the inverse of the absolute row sum matrix. That is
  * a diagonal operator composed of the inverse of the absolute value
  * of the row sum.
  *
  * \returns A diagonal operator.
  */
ModifiableLinearOp getAbsRowSumInvMatrix(const LinearOp & op)
{
   RCP<const Epetra_CrsMatrix> eCrsOp;

   try {
      // get Epetra_Operator
      RCP<const Epetra_Operator> eOp = Thyra::get_Epetra_Operator(*op);

      // cast it to a CrsMatrix
      eCrsOp = rcp_dynamic_cast<const Epetra_CrsMatrix>(eOp,true);
   }
   catch (std::exception & e) {
      RCP<Teuchos::FancyOStream> out = Teuchos::VerboseObjectBase::getDefaultOStream();

      *out << "PB: getAbsRowSumMatrix requires an Epetra_CrsMatrix\n";
      *out << "    Could not extract an Epetra_Operator from a \"" << op->description() << std::endl;
      *out << "           OR\n";
      *out << "    Could not cast an Epetra_Operator to a Epetra_CrsMatrix\n";
      *out << std::endl;
      *out << "*** THROWN EXCEPTION ***\n";
      *out << e.what() << std::endl;
      *out << "************************\n";
      
      throw e;
   }

   // extract diagonal
   const RCP<Epetra_Vector> ptrDiag = rcp(new Epetra_Vector(eCrsOp->RowMap()));
   Epetra_Vector & diag = *ptrDiag;
   
   // compute absolute value row sum
   diag.PutScalar(0.0);
   for(int i=0;i<eCrsOp->NumMyRows();i++) {
      double * values = 0;
      int numEntries;
      eCrsOp->ExtractMyRowView(i,numEntries,values);

      // build abs value row sum
      for(int j=0;j<numEntries;j++)
         diag[i] += std::abs(values[j]);
   }
   diag.Reciprocal(diag); // invert entries

   // build Thyra diagonal operator
   return PB::Epetra::thyraDiagOp(ptrDiag,eCrsOp->RowMap(),"absRowSumInv( " + op->getObjectLabel() + " )");
}

/** \brief Compute the lumped version of this matrix.
  *
  * Compute the lumped version of this matrix. That is
  * a diagonal operator composed of the row sum.
  *
  * \returns A diagonal operator.
  */
ModifiableLinearOp getLumpedMatrix(const LinearOp & op)
{
   RCP<Thyra::VectorBase<double> > ones = Thyra::createMember(op->domain());
   RCP<Thyra::VectorBase<double> > diag = Thyra::createMember(op->range());

   // set to all ones
   Thyra::assign(ones.ptr(),1.0);

   // compute lumped diagonal
   Thyra::apply(*op,Thyra::NONCONJ_ELE,*ones,&*diag);

   return rcp(new Thyra::DefaultDiagonalLinearOp<double>(diag));
}

/** \brief Compute the inverse of the lumped version of
  *        this matrix.
  *
  * Compute the inverse of the lumped version of this matrix.
  * That is a diagonal operator composed of the row sum.
  *
  * \returns A diagonal operator.
  */
ModifiableLinearOp getInvLumpedMatrix(const LinearOp & op)
{
   RCP<Thyra::VectorBase<double> > ones = Thyra::createMember(op->domain());
   RCP<Thyra::VectorBase<double> > diag = Thyra::createMember(op->range());

   // set to all ones
   Thyra::assign(ones.ptr(),1.0);

   // compute lumped diagonal
   Thyra::apply(*op,Thyra::NONCONJ_ELE,*ones,&*diag);
   Thyra::reciprocal(diag.ptr(),*diag);

   return rcp(new Thyra::DefaultDiagonalLinearOp<double>(diag));
}

/** \brief Get the diaonal of a linear operator
  *
  * Get the diagonal of a linear operator. Currently
  * it is assumed that the underlying operator is
  * an Epetra_RowMatrix.
  *
  * \param[in] op The operator whose diagonal is to be
  *               extracted.
  *
  * \returns An diagonal operator.
  */
const ModifiableLinearOp getDiagonalOp(const LinearOp & op)
{
   RCP<const Epetra_CrsMatrix> eCrsOp;

   try {
      // get Epetra_Operator
      RCP<const Epetra_Operator> eOp = Thyra::get_Epetra_Operator(*op);

      // cast it to a CrsMatrix
      eCrsOp = rcp_dynamic_cast<const Epetra_CrsMatrix>(eOp,true);
   }
   catch (std::exception & e) {
      RCP<Teuchos::FancyOStream> out = Teuchos::VerboseObjectBase::getDefaultOStream();

      *out << "PB: getDiagonalOp requires an Epetra_CrsMatrix\n";
      *out << "    Could not extract an Epetra_Operator from a \"" << op->description() << std::endl;
      *out << "           OR\n";
      *out << "    Could not cast an Epetra_Operator to a Epetra_CrsMatrix\n";
      *out << std::endl;
      *out << "*** THROWN EXCEPTION ***\n";
      *out << e.what() << std::endl;
      *out << "************************\n";
      
      throw e;
   }

   // extract diagonal
   const RCP<Epetra_Vector> diag = rcp(new Epetra_Vector(eCrsOp->RowMap()));
   TEST_FOR_EXCEPT(eCrsOp->ExtractDiagonalCopy(*diag));

   // build Thyra diagonal operator
   return PB::Epetra::thyraDiagOp(diag,eCrsOp->RowMap(),"diag( " + op->getObjectLabel() + " )");
}

const MultiVector getDiagonal(const LinearOp & op)
{
   RCP<const Epetra_CrsMatrix> eCrsOp;

   try {
      // get Epetra_Operator
      RCP<const Epetra_Operator> eOp = Thyra::get_Epetra_Operator(*op);

      // cast it to a CrsMatrix
      eCrsOp = rcp_dynamic_cast<const Epetra_CrsMatrix>(eOp,true);
   }
   catch (std::exception & e) {
      RCP<Teuchos::FancyOStream> out = Teuchos::VerboseObjectBase::getDefaultOStream();

      *out << "PB: getDiagonal requires an Epetra_CrsMatrix\n";
      *out << "    Could not extract an Epetra_Operator from a \"" << op->description() << std::endl;
      *out << "           OR\n";
      *out << "    Could not cast an Epetra_Operator to a Epetra_CrsMatrix\n";
      *out << std::endl;
      *out << "*** THROWN EXCEPTION ***\n";
      *out << e.what() << std::endl;
      *out << "************************\n";
      
      throw e;
   }

   // extract diagonal
   const RCP<Epetra_Vector> diag = rcp(new Epetra_Vector(eCrsOp->RowMap()));
   TEST_FOR_EXCEPT(eCrsOp->ExtractDiagonalCopy(*diag));

   return Thyra::create_Vector(diag,Thyra::create_VectorSpace(Teuchos::rcpFromRef(eCrsOp->RowMap())));
}

/** \brief Get the diaonal of a linear operator
  *
  * Get the inverse of the diagonal of a linear operator.
  * Currently it is assumed that the underlying operator is
  * an Epetra_RowMatrix.
  *
  * \param[in] op The operator whose diagonal is to be
  *               extracted and inverted
  *
  * \returns An diagonal operator.
  */
const ModifiableLinearOp getInvDiagonalOp(const LinearOp & op)
{
   RCP<const Epetra_CrsMatrix> eCrsOp;

   try {
      // get Epetra_Operator
      RCP<const Epetra_Operator> eOp = Thyra::get_Epetra_Operator(*op);

      // cast it to a CrsMatrix
      eCrsOp = rcp_dynamic_cast<const Epetra_CrsMatrix>(eOp,true);
   }
   catch (std::exception & e) {
      RCP<Teuchos::FancyOStream> out = Teuchos::VerboseObjectBase::getDefaultOStream();

      *out << "PB: getInvDiagonalOp requires an Epetra_CrsMatrix\n";
      *out << "    Could not extract an Epetra_Operator from a \"" << op->description() << std::endl;
      *out << "           OR\n";
      *out << "    Could not cast an Epetra_Operator to a Epetra_CrsMatrix\n";
      *out << std::endl;
      *out << "*** THROWN EXCEPTION ***\n";
      *out << e.what() << std::endl;
      *out << "************************\n";
      
      throw e;
   }

   // extract diagonal
   const RCP<Epetra_Vector> diag = rcp(new Epetra_Vector(eCrsOp->RowMap()));
   TEST_FOR_EXCEPT(eCrsOp->ExtractDiagonalCopy(*diag));
   diag->Reciprocal(*diag);

   // build Thyra diagonal operator
   return PB::Epetra::thyraDiagOp(diag,eCrsOp->RowMap(),"inv(diag( " + op->getObjectLabel() + " ))");
}

/** \brief Multiply three linear operators. 
  *
  * Multiply three linear operators. This currently assumes
  * that the underlying implementation uses Epetra_CrsMatrix.
  * The exception is that opm is allowed to be an diagonal matrix.
  *
  * \param[in] opl Left operator (assumed to be a Epetra_CrsMatrix)
  * \param[in] opm Middle operator (assumed to be a diagonal matrix)
  * \param[in] opr Right operator (assumed to be a Epetra_CrsMatrix)
  *
  * \returns Matrix product with a Epetra_CrsMatrix implementation
  */
const LinearOp explicitMultiply(const LinearOp & opl,const LinearOp & opm,const LinearOp & opr)
{
   // build implicit multiply
   const LinearOp implicitOp = Thyra::multiply(opl,opm,opr);

   // build transformer
   const RCP<Thyra::LinearOpTransformerBase<double> > prodTrans =
       Thyra::epetraExtDiagScaledMatProdTransformer();

   // build operator and multiply
   const RCP<Thyra::LinearOpBase<double> > explicitOp = prodTrans->createOutputOp();
   prodTrans->transform(*implicitOp,explicitOp.ptr());
   explicitOp->setObjectLabel("explicit( " + opl->getObjectLabel() +
                                     " * " + opm->getObjectLabel() +
                                     " * " + opr->getObjectLabel() + " )");

   return explicitOp;
}

/** \brief Multiply three linear operators. 
  *
  * Multiply three linear operators. This currently assumes
  * that the underlying implementation uses Epetra_CrsMatrix.
  * The exception is that opm is allowed to be an diagonal matrix.
  *
  * \param[in] opl Left operator (assumed to be a Epetra_CrsMatrix)
  * \param[in] opm Middle operator (assumed to be a Epetra_CrsMatrix or a diagonal matrix)
  * \param[in] opr Right operator (assumed to be a Epetra_CrsMatrix)
  * \param[in,out] destOp The operator to be used as the destination operator,
  *                       if this is null this function creates a new operator
  *
  * \returns Matrix product with a Epetra_CrsMatrix implementation
  */
const ModifiableLinearOp explicitMultiply(const LinearOp & opl,const LinearOp & opm,const LinearOp & opr,
                                          const ModifiableLinearOp & destOp)
{
   // build implicit multiply
   const LinearOp implicitOp = Thyra::multiply(opl,opm,opr);

   // build transformer
   const RCP<Thyra::LinearOpTransformerBase<double> > prodTrans =
       Thyra::epetraExtDiagScaledMatProdTransformer();

   // build operator destination operator
   ModifiableLinearOp explicitOp; 

   // if neccessary build a operator to put the explicit multiply into
   if(destOp==Teuchos::null)
      explicitOp = prodTrans->createOutputOp();
   else
      explicitOp = destOp;

   // perform multiplication
   prodTrans->transform(*implicitOp,explicitOp.ptr());

   // label it
   explicitOp->setObjectLabel("explicit( " + opl->getObjectLabel() +
                                     " * " + opm->getObjectLabel() +
                                     " * " + opr->getObjectLabel() + " )");

   return explicitOp;
}

/** \brief Multiply two linear operators. 
  *
  * Multiply two linear operators. This currently assumes
  * that the underlying implementation uses Epetra_CrsMatrix.
  *
  * \param[in] opl Left operator (assumed to be a Epetra_CrsMatrix)
  * \param[in] opr Right operator (assumed to be a Epetra_CrsMatrix)
  *
  * \returns Matrix product with a Epetra_CrsMatrix implementation
  */
const LinearOp explicitMultiply(const LinearOp & opl,const LinearOp & opr)
{
   // build implicit multiply
   const LinearOp implicitOp = Thyra::multiply(opl,opr);

   // build a scaling transformer
   RCP<Thyra::LinearOpTransformerBase<double> > prodTrans  
         = Thyra::epetraExtDiagScalingTransformer();

   // check to see if a scaling transformer works: if not use the 
   // DiagScaledMatrixProduct transformer
   if(not prodTrans->isCompatible(*implicitOp))
       prodTrans = Thyra::epetraExtDiagScaledMatProdTransformer();

   // build operator and multiply
   const RCP<Thyra::LinearOpBase<double> > explicitOp = prodTrans->createOutputOp();
   prodTrans->transform(*implicitOp,explicitOp.ptr());
   explicitOp->setObjectLabel("explicit( " + opl->getObjectLabel() +
                                     " * " + opr->getObjectLabel() + " )");

   return explicitOp;
}

/** \brief Multiply two linear operators. 
  *
  * Multiply two linear operators. This currently assumes
  * that the underlying implementation uses Epetra_CrsMatrix.
  * The exception is that opm is allowed to be an diagonal matrix.
  *
  * \param[in] opl Left operator (assumed to be a Epetra_CrsMatrix)
  * \param[in] opr Right operator (assumed to be a Epetra_CrsMatrix)
  * \param[in,out] destOp The operator to be used as the destination operator,
  *                       if this is null this function creates a new operator
  *
  * \returns Matrix product with a Epetra_CrsMatrix implementation
  */
const ModifiableLinearOp explicitMultiply(const LinearOp & opl,const LinearOp & opr,
                                          const ModifiableLinearOp & destOp)
{
   // build implicit multiply
   const LinearOp implicitOp = Thyra::multiply(opl,opr);

   // build transformer
   const RCP<Thyra::LinearOpTransformerBase<double> > prodTrans =
       Thyra::epetraExtDiagScaledMatProdTransformer();

   // build operator destination operator
   ModifiableLinearOp explicitOp; 

   // if neccessary build a operator to put the explicit multiply into
   if(destOp==Teuchos::null)
      explicitOp = prodTrans->createOutputOp();
   else
      explicitOp = destOp;

   // perform multiplication
   prodTrans->transform(*implicitOp,explicitOp.ptr());

   // label it
   explicitOp->setObjectLabel("explicit( " + opl->getObjectLabel() +
                                     " * " + opr->getObjectLabel() + " )");

   return explicitOp;
}

/** \brief Add two linear operators. 
  *
  * Add two linear operators. This currently assumes
  * that the underlying implementation uses Epetra_CrsMatrix.
  *
  * \param[in] opl Left operator (assumed to be a Epetra_CrsMatrix)
  * \param[in] opr Right operator (assumed to be a Epetra_CrsMatrix)
  *
  * \returns Matrix sum with a Epetra_CrsMatrix implementation
  */
const LinearOp explicitAdd(const LinearOp & opl,const LinearOp & opr)
{
   // build implicit multiply
   const LinearOp implicitOp = Thyra::add(opl,opr);
 
   // build transformer
   const RCP<Thyra::LinearOpTransformerBase<double> > prodTrans =
       Thyra::epetraExtAddTransformer();

   // build operator and multiply
   const RCP<Thyra::LinearOpBase<double> > explicitOp = prodTrans->createOutputOp();
   prodTrans->transform(*implicitOp,explicitOp.ptr());
   explicitOp->setObjectLabel("explicit( " + opl->getObjectLabel() +
                                     " + " + opr->getObjectLabel() + " )");

   return explicitOp;
}

/** \brief Add two linear operators. 
  *
  * Add two linear operators. This currently assumes
  * that the underlying implementation uses Epetra_CrsMatrix.
  *
  * \param[in] opl Left operator (assumed to be a Epetra_CrsMatrix)
  * \param[in] opr Right operator (assumed to be a Epetra_CrsMatrix)
  * \param[in,out] destOp The operator to be used as the destination operator,
  *                       if this is null this function creates a new operator
  *
  * \returns Matrix sum with a Epetra_CrsMatrix implementation
  */
const ModifiableLinearOp explicitAdd(const LinearOp & opl,const LinearOp & opr,
                                     const ModifiableLinearOp & destOp)
{
   // build implicit multiply
   const LinearOp implicitOp = Thyra::add(opl,opr);
 
   // build transformer
   const RCP<Thyra::LinearOpTransformerBase<double> > prodTrans =
       Thyra::epetraExtAddTransformer();

   // build or reuse destination operator
   RCP<Thyra::LinearOpBase<double> > explicitOp;
   if(destOp!=Teuchos::null)
      explicitOp = destOp;
   else
      explicitOp = prodTrans->createOutputOp();

   // perform multiplication
   prodTrans->transform(*implicitOp,explicitOp.ptr());
   explicitOp->setObjectLabel("explicit( " + opl->getObjectLabel() +
                                     " + " + opr->getObjectLabel() + " )");

   return explicitOp;
}

const LinearOp buildDiagonal(const MultiVector & src,const std::string & lbl)
{
   RCP<Thyra::VectorBase<double> > dst = Thyra::createMember(src->range()); 
   Thyra::copy(*src->col(0),dst.ptr());
   
   return Thyra::diagonal<double>(dst,lbl);
}

const LinearOp buildInvDiagonal(const MultiVector & src,const std::string & lbl)
{
   const RCP<const Thyra::VectorBase<double> > srcV = src->col(0);
   RCP<Thyra::VectorBase<double> > dst = Thyra::createMember(srcV->range()); 
   Thyra::reciprocal<double>(dst.ptr(),*srcV);

   return Thyra::diagonal<double>(dst,lbl);
}

//! build a BlockedMultiVector from a vector of MultiVectors
BlockedMultiVector buildBlockedMultiVector(const std::vector<MultiVector> & mvv)
{
   Teuchos::Array<MultiVector> mvA;
   Teuchos::Array<VectorSpace> vsA;

   // build arrays of multi vectors and vector spaces
   std::vector<MultiVector>::const_iterator itr;
   for(itr=mvv.begin();itr!=mvv.end();++itr) {
      mvA.push_back(*itr);
      vsA.push_back((*itr)->range());
   }

   // construct the product vector space
   const RCP<const Thyra::DefaultProductVectorSpace<double> > vs
         = Thyra::productVectorSpace<double>(vsA);

   return Thyra::defaultProductMultiVector<double>(vs,mvA);
}

/** \brief Compute the spectral radius of a matrix
  *
  * Compute the spectral radius of matrix A.  This utilizes the 
  * Trilinos-Anasazi BlockKrylovShcur method for computing eigenvalues.
  * It attempts to compute the largest (in magnitude) eigenvalue to a given
  * level of tolerance.
  *
  * \param[in] A   matrix whose spectral radius is needed
  * \param[in] tol The <em>most</em> accuracy needed (the algorithm will run until
  *            it reaches this level of accuracy and then it will quit).
  *            If this level is not reached it will return something to indicate
  *            it has not converged.
  * \param[out] eigVec Eigenvectors
  * \param[in] isHermitian Is the matrix Hermitian
  * \param[in] numBlocks The size of the orthogonal basis built (like in GMRES) before
  *                  restarting.  Increase the memory usage by O(restart*n). At least
  *                  restart=3 is required.
  * \param[in] restart How many restarts are permitted
  * \param[in] verbosity See the Anasazi documentation
  *
  * \return The spectral radius of the matrix.  If the algorithm didn't converge the
  *         number is the negative of the ritz-values. If a <code>NaN</code> is returned
  *         there was a problem constructing the Anasazi problem
  */
double computeSpectralRad(const RCP<const Thyra::LinearOpBase<double> > & A, double tol,
                          bool isHermitian,int numBlocks,int restart,int verbosity)
{
   typedef Thyra::LinearOpBase<double> OP;
   typedef Thyra::MultiVectorBase<double> MV;

   int startVectors = 1;

   // construct an initial guess
   const RCP<MV> ivec = Thyra::createMember(A->domain());
   Thyra::randomize(-1.0,1.0,&*ivec);
   
   RCP<Anasazi::BasicEigenproblem<double,MV,OP> > eigProb
         = rcp(new Anasazi::BasicEigenproblem<double,MV,OP>(A,ivec));
   eigProb->setNEV(1);
   eigProb->setHermitian(isHermitian);

   // set the problem up
   if(not eigProb->setProblem()) {
      // big time failure!
      return Teuchos::ScalarTraits<double>::nan();
   }

   // we want largert magnitude eigenvalue
   std::string which("LM"); // largest magnitude

   // Create the parameter list for the eigensolver
   // verbosity+=Anasazi::TimingDetails;
   Teuchos::ParameterList MyPL;
   MyPL.set( "Verbosity", verbosity );
   MyPL.set( "Which", which );
   MyPL.set( "Block Size", startVectors );
   MyPL.set( "Num Blocks", numBlocks );
   MyPL.set( "Maximum Restarts", restart ); 
   MyPL.set( "Convergence Tolerance", tol );

   // build status test manager
   // RCP<Anasazi::StatusTestMaxIters<double,MV,OP> > statTest
   //       = rcp(new Anasazi::StatusTestMaxIters<double,MV,OP>(numBlocks*(restart+1)));

   // Create the Block Krylov Schur solver
   // This takes as inputs the eigenvalue problem and the solver parameters
   Anasazi::BlockKrylovSchurSolMgr<double,MV,OP> MyBlockKrylovSchur(eigProb, MyPL );
 
   // Solve the eigenvalue problem, and save the return code
   Anasazi::ReturnType solverreturn = MyBlockKrylovSchur.solve();

   if(solverreturn==Anasazi::Unconverged) {
      // cout << "Anasazi::BlockKrylovSchur::solve() did not converge!" << std::endl; 
      return -std::abs(MyBlockKrylovSchur.getRitzValues().begin()->realpart);
   }
   else { // solverreturn==Anasazi::Converged
      // cout << "Anasazi::BlockKrylovSchur::solve() converged!" << endl;
      return std::abs(eigProb->getSolution().Evals.begin()->realpart);
   }
}

/** \brief Compute the smallest eigenvalue of an operator
  *
  * Compute the smallest eigenvalue of matrix A.  This utilizes the 
  * Trilinos-Anasazi BlockKrylovShcur method for computing eigenvalues.
  * It attempts to compute the smallest (in magnitude) eigenvalue to a given
  * level of tolerance.
  *
  * \param[in] A   matrix whose spectral radius is needed
  * \param[in] tol The <em>most</em> accuracy needed (the algorithm will run until
  *            it reaches this level of accuracy and then it will quit).
  *            If this level is not reached it will return something to indicate
  *            it has not converged.
  * \param[in] isHermitian Is the matrix Hermitian
  * \param[in] numBlocks The size of the orthogonal basis built (like in GMRES) before
  *                  restarting.  Increase the memory usage by O(restart*n). At least
  *                  restart=3 is required.
  * \param[in] restart How many restarts are permitted
  * \param[in] verbosity See the Anasazi documentation
  *
  * \return The smallest magnitude eigenvalue of the matrix.  If the algorithm didn't converge the
  *         number is the negative of the ritz-values. If a <code>NaN</code> is returned
  *         there was a problem constructing the Anasazi problem
  */
double computeSmallestMagEig(const RCP<const Thyra::LinearOpBase<double> > & A, double tol,
                             bool isHermitian,int numBlocks,int restart,int verbosity)
{
   typedef Thyra::LinearOpBase<double> OP;
   typedef Thyra::MultiVectorBase<double> MV;

   int startVectors = 1;

   // construct an initial guess
   const RCP<MV> ivec = Thyra::createMember(A->domain());
   Thyra::randomize(-1.0,1.0,&*ivec);
   
   RCP<Anasazi::BasicEigenproblem<double,MV,OP> > eigProb
         = rcp(new Anasazi::BasicEigenproblem<double,MV,OP>(A,ivec));
   eigProb->setNEV(1);
   eigProb->setHermitian(isHermitian);

   // set the problem up
   if(not eigProb->setProblem()) {
      // big time failure!
      return Teuchos::ScalarTraits<double>::nan();
   }

   // we want largert magnitude eigenvalue
   std::string which("SM"); // smallest magnitude

   // Create the parameter list for the eigensolver
   Teuchos::ParameterList MyPL;
   MyPL.set( "Verbosity", verbosity );
   MyPL.set( "Which", which );
   MyPL.set( "Block Size", startVectors );
   MyPL.set( "Num Blocks", numBlocks );
   MyPL.set( "Maximum Restarts", restart ); 
   MyPL.set( "Convergence Tolerance", tol );

   // build status test manager
   // RCP<Anasazi::StatusTestMaxIters<double,MV,OP> > statTest
   //       = rcp(new Anasazi::StatusTestMaxIters<double,MV,OP>(10));

   // Create the Block Krylov Schur solver
   // This takes as inputs the eigenvalue problem and the solver parameters
   Anasazi::BlockKrylovSchurSolMgr<double,MV,OP> MyBlockKrylovSchur(eigProb, MyPL );
 
   // Solve the eigenvalue problem, and save the return code
   Anasazi::ReturnType solverreturn = MyBlockKrylovSchur.solve();

   if(solverreturn==Anasazi::Unconverged) {
      // cout << "Anasazi::BlockKrylovSchur::solve() did not converge! " << std::endl;
      return -std::abs(MyBlockKrylovSchur.getRitzValues().begin()->realpart);
   }
   else { // solverreturn==Anasazi::Converged
      // cout << "Anasazi::BlockKrylovSchur::solve() converged!" << endl;
      return std::abs(eigProb->getSolution().Evals.begin()->realpart);
   }
}

/** Get a diagonal operator from a matrix. The mechanism for computing
  * the diagonal is specified by a <code>DiagonalType</code> arugment.
  *
  * \param[in] A <code>Epetra_CrsMatrix</code> to extract the diagonal from.
  * \param[in] dt Specifies the type of diagonal that is desired.
  *
  * \returns A diagonal operator.
  */
ModifiableLinearOp getDiagonalOp(PB::LinearOp & A,DiagonalType dt)
{
   switch(dt) {
   case Diagonal:
      return getDiagonalOp(A);  
   case Lumped:
      return getLumpedMatrix(A);  
   case AbsRowSum:
      return getAbsRowSumMatrix(A);  
   case NotDiag:
   default:
      TEST_FOR_EXCEPT(true);
   };

   return Teuchos::null;
}

/** Get the inverse of a diagonal operator from a matrix. The mechanism for computing
  * the diagonal is specified by a <code>DiagonalType</code> arugment.
  *
  * \param[in] A <code>Epetra_CrsMatrix</code> to extract the diagonal from.
  * \param[in] dt Specifies the type of diagonal that is desired.
  *
  * \returns A inverse of a diagonal operator.
  */
ModifiableLinearOp getInvDiagonalOp(PB::LinearOp & A,PB::DiagonalType dt)
{
   switch(dt) {
   case Diagonal:
      return getInvDiagonalOp(A);  
   case Lumped:
      return getInvLumpedMatrix(A);  
   case AbsRowSum:
      return getAbsRowSumInvMatrix(A);  
   case NotDiag:
   default:
      TEST_FOR_EXCEPT(true);
   };

   return Teuchos::null;
}

/** Get a string corresponding to the type of diagonal specified.
  *
  * \param[in] dt The type of diagonal.
  *
  * \returns A string name representing this diagonal type.
  */
std::string getDiagonalName(DiagonalType dt)
{
   switch(dt) {
   case Diagonal:
      return "Diagonal";
   case Lumped:
      return "Lumped";
   case AbsRowSum:
      return "AbsRowSum";
   case NotDiag:
      return "NotDiag";
   };

   return "<error>";
}

/** Get a type corresponding to the name of a diagonal specified.
  *
  * \param[in] name String representing the diagonal type
  *
  * \returns The type representation of the string, if the
  *          string is not recognized this function returns
  *          a <code>NotDiag</code>
  */
DiagonalType getDiagonalType(std::string name)
{
   if(name=="Diagonal")
      return Diagonal;
   if(name=="Lumped")
      return Lumped;
   if(name=="AbsRowSum")
      return AbsRowSum;
 
   return NotDiag;
}

}
