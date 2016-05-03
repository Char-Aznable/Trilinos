// @HEADER
//
// ***********************************************************************
//
//             Xpetra: A linear algebra interface package
//                  Copyright 2012 Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
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
// Questions? Contact
//                    Jonathan Hu       (jhu@sandia.gov)
//                    Andrey Prokopenko (aprokop@sandia.gov)
//                    Ray Tuminaro      (rstumin@sandia.gov)
//                    Tobias Wiesner    (tawiesn@sandia.gov)
//
// ***********************************************************************
//
// @HEADER
#ifndef XPETRA_BLOCKREORDERMANAGER_HPP_
#define XPETRA_BLOCKREORDERMANAGER_HPP_

#include <stack>

#include <Teuchos_StrUtils.hpp>

namespace Xpetra {
class BlockReorderManager {
public:
  //! @name Constructors

  //! Basic empty constructor
  BlockReorderManager() : children_(0) {}

  //! Copy constructor
  BlockReorderManager(const BlockReorderManager & bmm) :
    children_(bmm.children_.size()) {
    for(size_t i = 0; i < children_.size(); i++) children_[i] = bmm.children_[i]->Copy();
  }

  //! empty destructor
  virtual ~BlockReorderManager() { }

  //@}

  //! returns copy of this object
  virtual Teuchos::RCP<BlockReorderManager> Copy() const {
    return Teuchos::rcp(new BlockReorderManager(*this));
  }

  //! Sets number of subblocks
  virtual void SetNumBlocks( size_t sz ) {
    children_.clear(); children_.resize(sz);
  }

  //! Returns number of subblocks
  virtual size_t GetNumBlocks() const {
    return children_.size();
  }

  //! \brief Sets the subblock to a specific index value
  /**
   * Sets the subblock to a specific index value
   * \param[in] blockIndex: the subblock to be set
   * \param[in] reorder:    the value of the index of this subblock
   */
  virtual void SetBlock(int blockIndex, int reorder);/* {
    TEUCHOS_ASSERT(blockIndex < (int) children_.size());
    Teuchos::RCP<BlockReorderManager> child = Teuchos::rcp(new BlockReorderLeaf(reorder));
    children_[blockIndex] = child;
  }*/

  //! \brief Sets the subblock to a specific index value
  /**
   * Sets the subblock to a specific index value
   * \param[in] blockIndex: the subblock to be set
   * \param[in] reorder:    reorder manager for nested reordering
   */
  virtual void SetBlock(int blockIndex, const Teuchos::RCP<BlockReorderManager> & reorder);/* {
    TEUCHOS_ASSERT(blockIndex < (int) children_.size());
    children_[blockIndex] = reorder;
  }*/

  //! \brief Get a particular block. If there is no block at this index location return a new one
  virtual const Teuchos::RCP<BlockReorderManager> GetBlock(int blockIndex) {
    TEUCHOS_ASSERT(blockIndex<(int) children_.size());

    if(children_[blockIndex]==Teuchos::null)
      children_[blockIndex] = Teuchos::rcp(new BlockReorderManager());
    return children_[blockIndex];
  }

  //! \brief Get a particular block. If there is no block at this index location return a new one
  virtual const Teuchos::RCP<const BlockReorderManager> GetBlock(int blockIndex) const {
    TEUCHOS_ASSERT(blockIndex<(int) children_.size());
    return children_[blockIndex];
  }

  //! for sanities sake, print a readable string
  virtual std::string toString() const {
    // build the string by recursively calling each child
    std::stringstream ss;
    ss << "[";
    for(size_t i = 0; i < children_.size(); i++) {
      if(children_[i] == Teuchos::null)
        ss << " <NULL> ";
      else
        ss << " " << children_[i]->toString() << " ";
    }
    ss << "]";
    return ss.str();
  }

  //! returns largest index in this BlockReorderManager class
  virtual int LargestIndex() const {
    int max = 0;
    for(size_t i = 0; i<children_.size(); i++) {
      if(children_[i]!=Teuchos::null) {
        int subMax = children_[i]->LargestIndex();
        max = max > subMax ? max : subMax;
      }
    }
    return max;
  }

protected:
  //! definitions of the subblocks
  std::vector<Teuchos::RCP<BlockReorderManager> > children_;
};

class BlockReorderLeaf : public BlockReorderManager {
public:
  BlockReorderLeaf(int ind) : value_(ind) {}
  BlockReorderLeaf(const BlockReorderLeaf & brl) : value_(brl.value_) {}

  virtual Teuchos::RCP<BlockReorderManager> Copy() const {
    return Teuchos::rcp(new BlockReorderLeaf(*this));
  }

  virtual size_t GetNumBlocks() const { return 0; }
  virtual void SetNumBlocks(size_t sz) {}
  virtual void SetBlock(int blockIndex, int reorder) { }
  virtual void SetBlock(int blockIndex, const Teuchos::RCP<BlockReorderManager> & reorder) {}
  virtual const Teuchos::RCP<BlockReorderManager> GetBlock(int blockIndex) {
    return Teuchos::null;
  }
  virtual const Teuchos::RCP<const BlockReorderManager> GetBlock(int blockIndex)const {
    return Teuchos::null;
  }
  //! Get the index that is stored in this block/leaf
  int GetIndex() const { return value_; }
  virtual std::string toString() const {
    std::stringstream ss; ss << value_; return ss.str();
  }
  virtual int LargestIndex() const { return value_; }
protected:
  //! The value of the index for this leaf
  int value_;

private:
  BlockReorderLeaf() : value_(0) {}; // hidden from users
};

void BlockReorderManager::SetBlock(int blockIndex, int reorder) {
  TEUCHOS_ASSERT(blockIndex < (int) children_.size());
  Teuchos::RCP<BlockReorderManager> child = Teuchos::rcp(new BlockReorderLeaf(reorder));
  children_[blockIndex] = child;
}

void BlockReorderManager::SetBlock(int blockIndex, const Teuchos::RCP<BlockReorderManager> & reorder) {
  TEUCHOS_ASSERT(blockIndex < (int) children_.size());
  children_[blockIndex] = reorder;
}

// this function tokenizes a string, breaking out whitespace but saving the
// brackets [,] as special tokens.
void tokenize(std::string srcInput,std::string whitespace,std::string prefer, std::vector<std::string> & tokens) {
   std::string input = srcInput;
   std::vector<std::string> wsTokens;
   std::size_t endPos   = input.length()-1;
   while(endPos<input.length()) {
      std::size_t next = input.find_first_of(whitespace);

      // get the sub string
      std::string s;
      if(next!=std::string::npos) {
         s = input.substr(0,next);

         // break out the old substring
         input = input.substr(next+1,endPos);
      }
      else {
         s = input;
         input = "";
      }

      endPos   = input.length()-1;

      // add it to the WS tokens list
      if(s=="") continue;
      wsTokens.push_back(s);
   }

   for(unsigned int i=0;i<wsTokens.size();i++) {
      // get string to break up
      input = wsTokens[i];

      endPos   = input.length()-1;
      while(endPos<input.length()) {
         std::size_t next = input.find_first_of(prefer);

         std::string s = input;
         if(next>0 && next<input.length()) {

            // get the sub string
            s = input.substr(0,next);

            input = input.substr(next,endPos);
         }
         else if(next==0) {
            // get the sub string
            s = input.substr(0,next+1);

            input = input.substr(next+1,endPos);
         }
         else input = "";

         // break out the old substring
         endPos   = input.length()-1;

         // add it to the tokens list
         tokens.push_back(s);
      }
   }
}

// this function takes a set of tokens and returns the first "block", i.e. those
// values (including) brackets that correspond to the first block
std::vector<std::string>::const_iterator buildSubBlock(
    std::vector<std::string>::const_iterator begin,
    std::vector<std::string>::const_iterator end,
    std::vector<std::string> & subBlock) {
   std::stack<std::string> matched;
   std::vector<std::string>::const_iterator itr;
   for(itr=begin;itr!=end;++itr) {

      subBlock.push_back(*itr);

      // push/pop brackets as they are discovered
      if(*itr=="[") matched.push("[");
      else if(*itr=="]") matched.pop();

      // found all matching brackets
      if(matched.empty())
         return itr;
   }

   TEUCHOS_ASSERT(matched.empty());

   return itr-1;
}

// This function takes a tokenized vector and converts it to a block reorder manager
Teuchos::RCP<Xpetra::BlockReorderManager> blockedReorderFromTokens(const std::vector<std::string> & tokens)
{
   // base case
   if(tokens.size()==1)
      return Teuchos::rcp(new Xpetra::BlockReorderLeaf(Teuchos::StrUtils::atoi(tokens[0])));

   // check first and last character
   TEUCHOS_ASSERT(*(tokens.begin())=="[")
   TEUCHOS_ASSERT(*(tokens.end()-1)=="]");

   std::vector<Teuchos::RCP<Xpetra::BlockReorderManager> > vecRMgr;
   std::vector<std::string>::const_iterator itr = tokens.begin()+1;
   while(itr!=tokens.end()-1) {
      // figure out which tokens are relevant for this block
      std::vector<std::string> subBlock;
      itr = buildSubBlock(itr,tokens.end()-1,subBlock);

      // build the child block reorder manager
      vecRMgr.push_back(blockedReorderFromTokens(subBlock));

      // move the iterator one more
      itr++;
   }

   // build the parent reorder manager
   Teuchos::RCP<Xpetra::BlockReorderManager> rMgr = Teuchos::rcp(new Xpetra::BlockReorderManager());
   rMgr->SetNumBlocks(vecRMgr.size());
   for(unsigned int i=0;i<vecRMgr.size();i++)
      rMgr->SetBlock(i,vecRMgr[i]);

   return rMgr;
}

/** \brief Convert a string to a block reorder manager object
  *
  * Convert a string to a block reorder manager object. These
  * strings have numbers delimted by [,]. For example,
  * the string "[[2 1] 0]" will give a manager with [2 1] in the
  * first block and 0 in the second block.
  *
  * \param[in] reorder Block structure corresponding to the manager
  *
  * \returns A block reorder manager with the requested structure
  */
Teuchos::RCP<const Xpetra::BlockReorderManager> blockedReorderFromString(std::string reorder)
{
   // vector of tokens to use
   std::vector<std::string> tokens;

   // manager to be returned

   // build tokens vector
   tokenize(reorder," \t\n","[]",tokens);

   // parse recursively and build reorder manager
   Teuchos::RCP<Xpetra::BlockReorderManager> mgr = blockedReorderFromTokens(tokens);

   return mgr;
}

}

#endif /* XPETRA_BLOCKREORDERMANAGER_HPP_ */
