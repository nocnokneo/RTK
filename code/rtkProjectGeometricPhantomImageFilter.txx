/*=========================================================================
 *
 *  Copyright RTK Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

#ifndef __rtkProjectGeometricPhantomImageFilter_txx
#define __rtkProjectGeometricPhantomImageFilter_txx

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <itkImageRegionConstIterator.h>
#include <itkImageRegionIteratorWithIndex.h>
#include "rtkHomogeneousMatrix.h"

namespace rtk
{
template< class TInputImage, class TOutputImage >
void ProjectGeometricPhantomImageFilter< TInputImage, TOutputImage >::GenerateData()
{
  //Reading figure config file
  CFRType::Pointer cfr = CFRType::New();
  cfr->Config(m_ConfigFile);
  m_Fig = cfr->GetFig();
  //Reading type of figures
  std::vector<std::string> figType;
  figType = cfr->GetFigureTypes();

  unsigned int ellip = 0;
  unsigned int box   = 0;
  VectorType boxMin, boxMax;
  std::vector< typename REIType::Pointer > rei;
  std::vector< typename RBIType::Pointer > rbi;

  for ( unsigned int i = 0; i < m_Fig.size(); i++ )
  {
    // Ellipsoid, Cylinder and Cone Case
    if(figType[i]!="Box")
    {
      rei.push_back( REIType::New() );
      //Set GrayScale value, axes, center...
      rei[ellip]->SetDensity(m_Fig[i][8]);
      VectorType semiprincipalaxis;
      semiprincipalaxis[0] = m_Fig[i][1];
      semiprincipalaxis[1] = m_Fig[i][2];
      semiprincipalaxis[2] = m_Fig[i][3];
      if(figType[i]=="Cone")
        rei[ellip]->SetFigure("Cone");
      else
        rei[ellip]->SetFigure("Ellipsoid");
      rei[ellip]->SetAxis(semiprincipalaxis);

      VectorType center;
      center[0] = m_Fig[i][4];
      center[1] = m_Fig[i][5];
      center[2] = m_Fig[i][6];
      rei[ellip]->SetCenter(center);

      rei[ellip]->SetAngle(m_Fig[i][7]);

      if ( ellip == ( m_Fig.size() - 1 ) ) //last case
      {
        if(ellip==0) //just one ellipsoid
          rei[ellip]->SetInput( rei[ellip]->GetOutput() );
        else
          rei[ellip]->SetInput( rei[ellip-1]->GetOutput() );
      }

      if (ellip>0) //other cases
      {
        rei[ellip]->SetInput( rei[ellip-1]->GetOutput() );
      }

      else //first case
      {
        rei[ellip]->SetInput( this->GetInput() );
      }
      rei[ellip]->SetGeometry( this->GetGeometry() );
      ellip++;
    }
    // Box Case
    else if (figType[i]=="Box")
    {
      rbi.push_back( RBIType::New() );
      //Set GrayScale value, boxmax, boxmin, center...
      rbi[box]->SetDensity(m_Fig[i][8]);
      boxMin[0] = -m_Fig[i][1];
      boxMin[1] = -m_Fig[i][2];
      boxMin[2] = -m_Fig[i][3];
      boxMax[0] = m_Fig[i][1];
      boxMax[1] = m_Fig[i][2];
      boxMax[2] = m_Fig[i][3];
      rbi[box]->SetBoxMin(boxMin);
      rbi[box]->SetBoxMax(boxMax);

      // FIXME: add center location and rotation
      //rbi[box]->SetCenterY(m_Fig.parameters[i][4])
      //rbi[box]->SetCenterY(m_Fig.parameters[i][5]);
      //rbi[box]->SetCenterZ(m_Fig.parameters[i][6]);
      //rbi[box]->SetAngle(m_Fig.parameters[i][7]);

      if ( box == ( m_Fig.size() - 1 ) ) //last case
      {
        if(box==0) //just one box
          rbi[box]->SetInput( this->GetInput() );
        else
          rbi[box]->SetInput( rbi[box-1]->GetOutput() );
      }

      if (box>0) //other cases
      {
        rbi[box]->SetInput( rbi[box-1]->GetOutput() );
      }

      else //first case
      {
        rbi[box]->SetInput( this->GetInput() );
      }
      rbi[box]->SetGeometry( this->GetGeometry() );
      box++;
    }
    else
    {
      itkGenericExceptionMacro(<< "Unknown Figure type ( " << figType[i] )
    }
  }
  //Add Image Filter used to concatenate the different figures obtained on each iteration
  typename AddImageFilterType::Pointer addFilter = AddImageFilterType::New();
  if(box)
  {
    rbi[box-1]->Update();
    addFilter->SetInput1(rbi[box-1]->GetOutput());
  }
  else
    addFilter->SetInput1(this->GetInput());
  if(ellip)
  {
    rei[ellip-1]->Update();
    addFilter->SetInput2(rei[ellip-1]->GetOutput());
  }
  else
    addFilter->SetInput1(this->GetInput());
  addFilter->Update();
  this->GraftOutput( addFilter->GetOutput() );
}

template< class TInputImage, class TOutputImage >
typename ProjectGeometricPhantomImageFilter< TInputImage, TOutputImage >::VectorOfVectorType
ProjectGeometricPhantomImageFilter< TInputImage, TOutputImage >::GetFig()
{
  itkDebugMacro("returning Fig.");
  return this->m_Fig;
}

template< class TInputImage, class TOutputImage >
void
ProjectGeometricPhantomImageFilter< TInputImage, TOutputImage >::SetFig(const VectorOfVectorType _arg)
{
  itkDebugMacro("setting Fig");
  if (this->m_Fig != _arg)
    {
    this->m_Fig = _arg;
    this->Modified();
    }
}

} // end namespace rtk

#endif
