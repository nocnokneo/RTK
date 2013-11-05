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

#ifndef __rtkAmsterdamShroudImageFilter_txx
#define __rtkAmsterdamShroudImageFilter_txx

#include <itkImageFileWriter.h>

namespace rtk
{

template <class TInputImage>
AmsterdamShroudImageFilter<TInputImage>
::AmsterdamShroudImageFilter():
  m_UnsharpMaskSize(17)
{
  m_DerivativeFilter = DerivativeType::New();
  m_NegativeFilter = NegativeType::New();
  m_ThresholdFilter = ThresholdType::New();
  m_SumFilter = SumType::New();
  m_ConvolutionFilter = ConvolutionType::New();
  m_SubtractFilter = SubtractType::New();
  m_PermuteFilter = PermuteType::New();

  m_NegativeFilter->SetInput( m_DerivativeFilter->GetOutput() );
  m_ThresholdFilter->SetInput( m_NegativeFilter->GetOutput() );
  m_SumFilter->SetInput( m_ThresholdFilter->GetOutput() );
  m_ConvolutionFilter->SetInput( m_SumFilter->GetOutput() );
  m_SubtractFilter->SetInput1( m_SumFilter->GetOutput() );
  m_SubtractFilter->SetInput2( m_ConvolutionFilter->GetOutput() );
  m_PermuteFilter->SetInput( m_SubtractFilter->GetOutput() );

  m_DerivativeFilter->SetOrder(DerivativeType::FirstOrder);
  m_DerivativeFilter->SetDirection(1);
  m_DerivativeFilter->SetSigma(4);

  m_NegativeFilter->SetConstant(-1.0);
  m_NegativeFilter->InPlaceOn();

  m_ThresholdFilter->SetUpper(0.0);
  m_ThresholdFilter->SetOutsideValue(0.0);
  m_ThresholdFilter->InPlaceOn();

  m_SumFilter->SetProjectionDimension(0);

  // The permute filter is used to allow streaming because ITK splits the output image in the last direction
  typename PermuteType::PermuteOrderArrayType order;
  order[0] = 1;
  order[1] = 0;
  m_PermuteFilter->SetOrder(order);

  // Create default kernel
  UpdateUnsharpMaskKernel();
}

template <class TInputImage>
void
AmsterdamShroudImageFilter<TInputImage>
::GenerateOutputInformation()
{
  // get pointers to the input and output
  typename Superclass::InputImageConstPointer inputPtr  = this->GetInput();
  typename Superclass::OutputImagePointer     outputPtr = this->GetOutput();

  if ( !outputPtr || !inputPtr)
    {
    return;
    }

  m_DerivativeFilter->SetInput( this->GetInput() );
  m_PermuteFilter->UpdateOutputInformation();
  outputPtr->SetLargestPossibleRegion( m_PermuteFilter->GetOutput()->GetLargestPossibleRegion() );
}

template <class TInputImage>
void
AmsterdamShroudImageFilter<TInputImage>
::GenerateInputRequestedRegion()
{
  typename Superclass::InputImagePointer  inputPtr =
    const_cast< TInputImage * >( this->GetInput() );
  if ( !inputPtr )
    {
    return;
    }
  m_DerivativeFilter->SetInput( this->GetInput() );
  m_PermuteFilter->GetOutput()->SetRequestedRegion(this->GetOutput()->GetRequestedRegion() );
}

template<class TInputImage>
void
AmsterdamShroudImageFilter<TInputImage>
::GenerateData()
{
  unsigned int kernelWidth;
#if ITK_VERSION_MAJOR <= 3
  kernelWidth = m_ConvolutionFilter->GetImageKernelInput()->GetLargestPossibleRegion().GetSize()[1];
#else
  kernelWidth = m_ConvolutionFilter->GetKernelImage()->GetLargestPossibleRegion().GetSize()[1];
#endif
  if(kernelWidth != m_UnsharpMaskSize)
    UpdateUnsharpMaskKernel();
  m_PermuteFilter->Update();
  this->GraftOutput( m_PermuteFilter->GetOutput() );
}

template<class TInputImage>
void
AmsterdamShroudImageFilter<TInputImage>
::UpdateUnsharpMaskKernel()
{
  // Unsharp mask: difference between image and moving average
  // m_ConvolutionFilter computes the moving average
  typename TOutputImage::Pointer kernel = TOutputImage::New();
  typename TOutputImage::RegionType region;
  region.SetIndex(0, 0);
  region.SetIndex(1, (int)m_UnsharpMaskSize/-2);
  region.SetSize(0, 1);
  region.SetSize(1, m_UnsharpMaskSize);
  kernel->SetRegions(region);
  kernel->Allocate();
  kernel->FillBuffer(1./m_UnsharpMaskSize);
  #if ITK_VERSION_MAJOR <= 3
  m_ConvolutionFilter->SetImageKernelInput( kernel );
  #else
  m_ConvolutionFilter->SetKernelImage( kernel );
  #endif
}

} // end namespace rtk

#endif
