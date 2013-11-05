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

#ifndef __rtkDrawSheppLoganFilter_txx
#define __rtkDrawSheppLoganFilter_txx

#include <iostream>
#include <itkImageRegionConstIterator.h>
#include <itkImageRegionIterator.h>

#include "rtkHomogeneousMatrix.h"

#include "rtkMacro.h"

namespace rtk
{

template <class TInputImage, class TOutputImage>
DrawSheppLoganFilter<TInputImage, TOutputImage>
::DrawSheppLoganFilter():
m_PhantomScale(128.0)
{
}

template <class TInputImage, class TOutputImage>
void DrawSheppLoganFilter<TInputImage, TOutputImage>::ThreadedGenerateData(const OutputImageRegionType& outputRegionForThread,
                                                                             ThreadIdType itkNotUsed(threadId) )
{
  //Getting phantom parameters
  EQPFunctionType::Pointer sqpFunctor = EQPFunctionType::New();
  FigureType figParam;
  std::vector< FigureType > shepplogan(10, figParam);
  unsigned int NumberOfFig = 10;

  shepplogan[0].semiprincipalaxis[0] = 0.69*m_PhantomScale;
  shepplogan[0].semiprincipalaxis[1] = 0.90*m_PhantomScale;
  shepplogan[0].semiprincipalaxis[2] = 0.92*m_PhantomScale;
  shepplogan[0].center[0] = 0.;
  shepplogan[0].center[1] = 0.;
  shepplogan[0].center[2] = 0.;
  shepplogan[0].angle = 0.;
  shepplogan[0].attenuation = 2.;

  shepplogan[1].semiprincipalaxis[0] = 0.6624*m_PhantomScale;
  shepplogan[1].semiprincipalaxis[1] = 0.880*m_PhantomScale;
  shepplogan[1].semiprincipalaxis[2] = 0.874*m_PhantomScale;
  shepplogan[1].center[0] = 0.;
  shepplogan[1].center[1] = 0.;
  shepplogan[1].center[2] = 0.;
  shepplogan[1].angle = 0.;
  shepplogan[1].attenuation = -0.98;

  shepplogan[2].semiprincipalaxis[0] = 0.41*m_PhantomScale;
  shepplogan[2].semiprincipalaxis[1] = 0.21*m_PhantomScale;
  shepplogan[2].semiprincipalaxis[2] = 0.16*m_PhantomScale;
  shepplogan[2].center[0] = -0.22*m_PhantomScale;
  shepplogan[2].center[1] = -0.25*m_PhantomScale;
  shepplogan[2].center[2] = 0.;
  shepplogan[2].angle = 108.;
  shepplogan[2].attenuation = -0.02;

  shepplogan[3].semiprincipalaxis[0] = 0.31*m_PhantomScale;
  shepplogan[3].semiprincipalaxis[1] = 0.22*m_PhantomScale;
  shepplogan[3].semiprincipalaxis[2] = 0.11*m_PhantomScale;
  shepplogan[3].center[0] = 0.22*m_PhantomScale;
  shepplogan[3].center[1] = -0.25*m_PhantomScale;
  shepplogan[3].center[2] = 0.;
  shepplogan[3].angle = 72.;
  shepplogan[3].attenuation = -0.02;

  shepplogan[4].semiprincipalaxis[0] = 0.21*m_PhantomScale;
  shepplogan[4].semiprincipalaxis[1] = 0.50*m_PhantomScale;
  shepplogan[4].semiprincipalaxis[2] = 0.25*m_PhantomScale;
  shepplogan[4].center[0] = 0.;
  shepplogan[4].center[1] = -0.25*m_PhantomScale;
  shepplogan[4].center[2] = 0.35*m_PhantomScale;
  shepplogan[4].angle = 0.;
  shepplogan[4].attenuation = 0.02;

  shepplogan[5].semiprincipalaxis[0] = 0.046*m_PhantomScale;
  shepplogan[5].semiprincipalaxis[1] = 0.046*m_PhantomScale;
  shepplogan[5].semiprincipalaxis[2] = 0.046*m_PhantomScale;
  shepplogan[5].center[0] = 0.;
  shepplogan[5].center[1] = -0.25*m_PhantomScale;
  shepplogan[5].center[2] = 0.10*m_PhantomScale;
  shepplogan[5].angle = 0.;
  shepplogan[5].attenuation = 0.02;

  shepplogan[6].semiprincipalaxis[0] = 0.046*m_PhantomScale;
  shepplogan[6].semiprincipalaxis[1] = 0.020*m_PhantomScale;
  shepplogan[6].semiprincipalaxis[2] = 0.023*m_PhantomScale;
  shepplogan[6].center[0] = -0.08*m_PhantomScale;
  shepplogan[6].center[1] = -0.250*m_PhantomScale;
  shepplogan[6].center[2] = -0.650*m_PhantomScale;
  shepplogan[6].angle = 0.;
  shepplogan[6].attenuation = 0.01;

  shepplogan[7].semiprincipalaxis[0] = 0.046*m_PhantomScale;
  shepplogan[7].semiprincipalaxis[1] = 0.020*m_PhantomScale;
  shepplogan[7].semiprincipalaxis[2] = 0.023*m_PhantomScale;
  shepplogan[7].center[0] = 0.06*m_PhantomScale;
  shepplogan[7].center[1] = -0.25*m_PhantomScale;
  shepplogan[7].center[2] = -0.65*m_PhantomScale;
  shepplogan[7].angle = 90.;
  shepplogan[7].attenuation = 0.01;

  shepplogan[8].semiprincipalaxis[0] = 0.056*m_PhantomScale;
  shepplogan[8].semiprincipalaxis[1] = 0.010*m_PhantomScale;
  shepplogan[8].semiprincipalaxis[2] = 0.040*m_PhantomScale;
  shepplogan[8].center[0] = 0.060*m_PhantomScale;
  shepplogan[8].center[1] = 0.625*m_PhantomScale;
  shepplogan[8].center[2] = -0.105*m_PhantomScale;
  shepplogan[8].angle = 90.;
  shepplogan[8].attenuation = 0.02;

  shepplogan[9].semiprincipalaxis[0] = 0.056*m_PhantomScale;
  shepplogan[9].semiprincipalaxis[1] = 0.100*m_PhantomScale;
  shepplogan[9].semiprincipalaxis[2] = 0.056*m_PhantomScale;
  shepplogan[9].center[0] = 0.;
  shepplogan[9].center[1] = 0.625*m_PhantomScale;
  shepplogan[9].center[2] = 0.100*m_PhantomScale;
  shepplogan[9].angle = 0.;
  shepplogan[9].attenuation = -0.02;


  typename TOutputImage::PointType point;
  const    TInputImage *           input = this->GetInput();

  for(unsigned int i=0; i<NumberOfFig; i++)
  {
    typename itk::ImageRegionConstIterator<TInputImage> itIn( input, outputRegionForThread);
    typename itk::ImageRegionIterator<TOutputImage> itOut(this->GetOutput(), outputRegionForThread);

    //Set type of Figure
    sqpFunctor->SetFigure("Ellipsoid");
    //Translate from regular expression to quadric
    sqpFunctor->Translate(shepplogan[i].semiprincipalaxis);
    //Applies rotation and translation if necessary
    sqpFunctor->Rotate(shepplogan[i].angle, shepplogan[i].center);
    while( !itOut.IsAtEnd() )
      {
      this->GetInput()->TransformIndexToPhysicalPoint(itOut.GetIndex(), point);

      double QuadricEllip = sqpFunctor->GetA()*point[0]*point[0]   +
                   sqpFunctor->GetB()*point[1]*point[1]   +
                   sqpFunctor->GetC()*point[2]*point[2]   +
                   sqpFunctor->GetD()*point[0]*point[1]   +
                   sqpFunctor->GetE()*point[0]*point[2]   +
                   sqpFunctor->GetF()*point[1]*point[2]   +
                   sqpFunctor->GetG()*point[0] + sqpFunctor->GetH()*point[1] +
                   sqpFunctor->GetI()*point[2] + sqpFunctor->GetJ();
      if(QuadricEllip<0){
        itOut.Set(shepplogan[i].attenuation + itIn.Get());
      }
      else if (i==0)
        itOut.Set(0.);
      ++itIn;
      ++itOut;
      }
    input = this->GetOutput();
  }
}

}// end namespace rtk

#endif
