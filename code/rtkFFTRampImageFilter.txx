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

#ifndef __rtkFFTRampImageFilter_txx
#define __rtkFFTRampImageFilter_txx

// Use local RTK FFTW files taken from Gaëtan Lehmann's code for
// thread safety: http://hdl.handle.net/10380/3154
#if ITK_VERSION_MAJOR <= 3
#  if defined(USE_FFTWD) || defined(USE_FFTWF)
#    include "itkFFTWRealToComplexConjugateImageFilter.h"
#    include "itkFFTWComplexConjugateToRealImageFilter.h"
#  endif
#  include <itkFFTComplexConjugateToRealImageFilter.h>
#  include <itkFFTRealToComplexConjugateImageFilter.h>
#else
#  include <itkRealToHalfHermitianForwardFFTImageFilter.h>
#  include <itkHalfHermitianToRealInverseFFTImageFilter.h>
#endif

#include <itkImageRegionIterator.h>
#include <itkImageRegionIteratorWithIndex.h>

namespace rtk
{

template <class TInputImage, class TOutputImage, class TFFTPrecision>
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::FFTRampImageFilter() :
  m_TruncationCorrection(0.), m_GreatestPrimeFactor(2), m_HannCutFrequency(0.),
  m_CosineCutFrequency(0.),m_HammingFrequency(0.), m_HannCutFrequencyY(0.),
  m_BackupNumberOfThreads(1)
{
#if defined(USE_FFTWD)
  if(typeid(TFFTPrecision).name() == typeid(double).name() )
    m_GreatestPrimeFactor = 13;
#endif
#if defined(USE_FFTWF)
  if(typeid(TFFTPrecision).name() == typeid(float).name() )
    m_GreatestPrimeFactor = 13;
#endif
}

template <class TInputImage, class TOutputImage, class TFFTPrecision>
void
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::GenerateInputRequestedRegion()
{
  // call the superclass' implementation of this method
  Superclass::GenerateInputRequestedRegion();

  InputImageType * input = const_cast<InputImageType *>(this->GetInput() );
  if ( !input )
    return;

  // Compute input region (==requested region fully enlarged for dim 0)
  RegionType inputRegion;
  this->CallCopyOutputRegionToInputRegion(inputRegion, this->GetOutput()->GetRequestedRegion() );
  inputRegion.SetIndex(0, this->GetOutput()->GetLargestPossibleRegion().GetIndex(0) );
  inputRegion.SetSize(0, this->GetOutput()->GetLargestPossibleRegion().GetSize(0) );

  // Also enlarge along dim 1 if hann is set in that direction
  if(m_HannCutFrequencyY>0.)
    {
    inputRegion.SetIndex(1, this->GetOutput()->GetLargestPossibleRegion().GetIndex(1) );
    inputRegion.SetSize(1, this->GetOutput()->GetLargestPossibleRegion().GetSize(1) );
    }
  input->SetRequestedRegion( inputRegion );
}

template<class TInputImage, class TOutputImage, class TFFTPrecision>
int
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::GetTruncationCorrectionExtent()
{
  return vnl_math_floor(m_TruncationCorrection * this->GetInput()->GetRequestedRegion().GetSize(0));
}

template<class TInputImage, class TOutputImage, class TFFTPrecision>
void
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::BeforeThreadedGenerateData()
{
  UpdateTruncationMirrorWeights();
  if(this->GetOutput()->GetRequestedRegion().GetSize()[2] == 1 &&
     this->GetHannCutFrequencyY() != 0.)
    {
    m_BackupNumberOfThreads = this->GetNumberOfThreads();
    this->SetNumberOfThreads(1);
    }
  else
    m_BackupNumberOfThreads = 1;
}

template<class TInputImage, class TOutputImage, class TFFTPrecision>
void
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::AfterThreadedGenerateData()
{
  this->SetNumberOfThreads(m_BackupNumberOfThreads);
}

template<class TInputImage, class TOutputImage, class TFFTPrecision>
void
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::ThreadedGenerateData( const RegionType& outputRegionForThread, ThreadIdType itkNotUsed(threadId) )
{
  typedef typename itk::Image<TFFTPrecision,
                              TInputImage::ImageDimension > FFTInputImageType;
  typedef typename FFTInputImageType::Pointer               FFTInputImagePointer;
  typedef typename itk::Image<std::complex<TFFTPrecision>,
                              TInputImage::ImageDimension > FFTOutputImageType;
  typedef typename FFTOutputImageType::Pointer              FFTOutputImagePointer;

  // Pad image region enlarged along X
  RegionType enlargedRegionX = outputRegionForThread;
  enlargedRegionX.SetIndex(0, this->GetInput()->GetRequestedRegion().GetIndex(0) );
  enlargedRegionX.SetSize(0, this->GetInput()->GetRequestedRegion().GetSize(0) );
  enlargedRegionX.SetIndex(1, this->GetInput()->GetRequestedRegion().GetIndex(1) );
  enlargedRegionX.SetSize(1, this->GetInput()->GetRequestedRegion().GetSize(1) );
  FFTInputImagePointer paddedImage;
  paddedImage = PadInputImageRegion<FFTInputImageType, FFTOutputImageType>(enlargedRegionX);

  // FFT padded image
#if ITK_VERSION_MAJOR <= 3
  typedef itk::FFTRealToComplexConjugateImageFilter< FFTPrecisionType, ImageDimension > FFTType;
#else
  typedef itk::RealToHalfHermitianForwardFFTImageFilter< FFTInputImageType > FFTType;
#endif
  typename FFTType::Pointer fftI = FFTType::New();
  fftI->SetInput( paddedImage );
  fftI->SetNumberOfThreads( m_BackupNumberOfThreads );
  fftI->Update();

  // Get FFT ramp kernel
  typename FFTOutputImageType::SizeType s;
  s = paddedImage->GetLargestPossibleRegion().GetSize();
  FFTOutputImagePointer fftK;
  fftK = this->GetFFTRampKernel<FFTInputImageType, FFTOutputImageType>(s[0], s[1]);

  //Multiply line-by-line
  itk::ImageRegionIterator<typename FFTType::OutputImageType> itI(fftI->GetOutput(),
                                                              fftI->GetOutput()->GetLargestPossibleRegion() );
  itk::ImageRegionConstIterator<FFTOutputImageType> itK(fftK, fftK->GetLargestPossibleRegion() );
  itI.GoToBegin();
  while(!itI.IsAtEnd() ) {
    itK.GoToBegin();
    while(!itK.IsAtEnd() ) {
      itI.Set(itI.Get() * itK.Get() );
      ++itI;
      ++itK;
      }
    }

  //Inverse FFT image
#if ITK_VERSION_MAJOR <= 3
  typedef itk::FFTComplexConjugateToRealImageFilter< FFTPrecisionType, ImageDimension > IFFTType;
#else
  typedef itk::HalfHermitianToRealInverseFFTImageFilter< typename FFTType::OutputImageType > IFFTType;
#endif
  typename IFFTType::Pointer ifft = IFFTType::New();
  ifft->SetInput( fftI->GetOutput() );
#if ITK_VERSION_MAJOR <= 3
  ifft->SetActualXDimensionIsOdd( paddedImage->GetLargestPossibleRegion().GetSize(0) % 2 );
#endif
  ifft->SetNumberOfThreads( m_BackupNumberOfThreads );
  ifft->SetReleaseDataFlag( true );
  ifft->Update();

  // Crop and paste result
  itk::ImageRegionConstIterator<FFTInputImageType> itS(ifft->GetOutput(), outputRegionForThread);
  itk::ImageRegionIterator<OutputImageType>        itD(this->GetOutput(), outputRegionForThread);
  itS.GoToBegin();
  itD.GoToBegin();
  while(!itS.IsAtEnd() ) {
    itD.Set(itS.Get() );
    ++itS;
    ++itD;
    }
}

template<class TInputImage, class TOutputImage, class TFFTPrecision>
template<class TFFTInputImage, class TFFTOutputImage>
typename TFFTInputImage::Pointer
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::PadInputImageRegion(const RegionType &inputRegion)
{
  UpdateTruncationMirrorWeights();

  RegionType paddedRegion = inputRegion;

  // Set x padding
  typename SizeType::SizeValueType xPaddedSize = 2*inputRegion.GetSize(0);
  while( GreatestPrimeFactor( xPaddedSize ) > m_GreatestPrimeFactor )
    xPaddedSize++;
  paddedRegion.SetSize(0, xPaddedSize);
  long zeroext = ( (long)xPaddedSize - (long)inputRegion.GetSize(0) ) / 2;
  paddedRegion.SetIndex(0, inputRegion.GetIndex(0) - zeroext);

  // Set y padding
  typename SizeType::SizeValueType yPaddedSize = inputRegion.GetSize(1);
  while( GreatestPrimeFactor( yPaddedSize ) > m_GreatestPrimeFactor )
    yPaddedSize++;
  paddedRegion.SetSize(1, yPaddedSize);
  paddedRegion.SetIndex(1, inputRegion.GetIndex(1) );

  // Create padded image (spacing and origin do not matter)
  typename TFFTInputImage::Pointer paddedImage = TFFTInputImage::New();
  paddedImage->SetRegions(paddedRegion);
  paddedImage->Allocate();
  paddedImage->FillBuffer(0);

  const long next = vnl_math_min(zeroext, (long)this->GetTruncationCorrectionExtent() );
  if(next)
    {
    typename TFFTInputImage::IndexType idx;
    typename TFFTInputImage::IndexType::IndexValueType borderDist=0, rightIdx=0;

    // Mirror left
    RegionType leftRegion = inputRegion;
    leftRegion.SetIndex(0, inputRegion.GetIndex(0)-next);
    leftRegion.SetSize(0, next);
    itk::ImageRegionIteratorWithIndex<TFFTInputImage> itLeft(paddedImage, leftRegion);
    while(!itLeft.IsAtEnd() )
      {
      idx = itLeft.GetIndex();
      borderDist = inputRegion.GetIndex(0)-idx[0];
      idx[0] = inputRegion.GetIndex(0) + borderDist;
      itLeft.Set(m_TruncationMirrorWeights[ borderDist ] * this->GetInput()->GetPixel(idx) );
      ++itLeft;
      }

    // Mirror right
    RegionType rightRegion = inputRegion;
    rightRegion.SetIndex(0, inputRegion.GetIndex(0)+inputRegion.GetSize(0) );
    rightRegion.SetSize(0, next);
    itk::ImageRegionIteratorWithIndex<TFFTInputImage> itRight(paddedImage, rightRegion);
    while(!itRight.IsAtEnd() )
      {
      idx = itRight.GetIndex();
      rightIdx = inputRegion.GetIndex(0)+inputRegion.GetSize(0)-1;
      borderDist = idx[0]-rightIdx;
      idx[0] = rightIdx - borderDist;
      itRight.Set(m_TruncationMirrorWeights[ borderDist ] * this->GetInput()->GetPixel(idx) );
      ++itRight;
      }
    }

  // Copy central part
  itk::ImageRegionConstIterator<InputImageType> itS(this->GetInput(), inputRegion);
  itk::ImageRegionIterator<TFFTInputImage>   itD(paddedImage, inputRegion);
  itS.GoToBegin();
  itD.GoToBegin();
  while(!itS.IsAtEnd() ) {
    itD.Set(itS.Get() );
    ++itS;
    ++itD;
    }

  return paddedImage;
}

template<class TInputImage, class TOutputImage, class TFFTPrecision>
void
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::PrintSelf(std::ostream &os, itk::Indent indent) const
{
  Superclass::PrintSelf(os, indent);
  os << indent << "GreatestPrimeFactor: "  << m_GreatestPrimeFactor << std::endl;
}

template<class TInputImage, class TOutputImage, class TFFTPrecision>
bool
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::IsPrime( int n ) const
{
  int last = (int)vcl_sqrt( double(n) );

  for( int x=2; x<=last; x++ )
    if( n%x == 0 )
      return false;
  return true;
}

template<class TInputImage, class TOutputImage, class TFFTPrecision>
int
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::GreatestPrimeFactor( int n ) const
{
  int v = 2;

  while( v <= n )
    if( n%v == 0 && IsPrime( v ) )
      n /= v;
    else
      v += 1;
  return v;
}

template<class TInputImage, class TOutputImage, class TFFTPrecision>
template<class TFFTInputImage, class TFFTOutputImage>
typename TFFTOutputImage::Pointer
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::GetFFTRampKernel(const int width, const int height)
{
  // Allocate kernel
  SizeType size;
  size.Fill(1);
  size[0] = width;
  typename TFFTInputImage::Pointer kernel = TFFTInputImage::New();
  kernel->SetRegions( size );
  kernel->Allocate();
  kernel->FillBuffer(0.);

  // Compute kernel in space domain (see Kak & Slaney, chapter 3 equation 61
  // page 72) although spacing is not squared according to equation 69 page 75
  double spacing = this->GetInput()->GetSpacing()[0];
  IndexType i,j;
  i.Fill(0);
  j.Fill(0);
  kernel->SetPixel(i, 1./(4.*spacing) );
  for(i[0]=1, j[0]=size[0]-1; i[0] < typename IndexType::IndexValueType(size[0]/2); i[0] += 2, j[0] -= 2)
    {
    double v = i[0] * vnl_math::pi;
    v = -1. / (v * v * spacing);
    kernel->SetPixel(i, v);
    kernel->SetPixel(j, v);
    }

  // FFT kernel
#if ITK_VERSION_MAJOR <= 3
  typedef itk::FFTRealToComplexConjugateImageFilter< FFTPrecisionType, ImageDimension > FFTType;
#else
  typedef itk::RealToHalfHermitianForwardFFTImageFilter< TFFTInputImage, TFFTOutputImage > FFTType;
#endif
  typename FFTType::Pointer fftK = FFTType::New();
  fftK->SetInput( kernel );
  fftK->SetNumberOfThreads( 1 );
  fftK->Update();

  // Windowing (if enabled)
  typedef itk::ImageRegionIteratorWithIndex<typename FFTType::OutputImageType> IteratorType;
  IteratorType itK(fftK->GetOutput(), fftK->GetOutput()->GetLargestPossibleRegion() );

  unsigned int n = fftK->GetOutput()->GetLargestPossibleRegion().GetSize(0);
#if ITK_VERSION_MAJOR <= 3
#  if !defined(USE_FFTWD)
  if( typeid(TFFTPrecision).name() == typeid(double).name() )
    n = n/2+1;
#  endif
#  if !defined(USE_FFTWF)
  if( typeid(TFFTPrecision).name() == typeid(float).name() )
    n = n/2+1;
#  endif
#endif

  itK.GoToBegin();
  if(this->GetHannCutFrequency()>0.)
    {
    const unsigned int ncut = itk::Math::Round<double>(n * vnl_math_min(1.0, this->GetHannCutFrequency() ) );
    for(unsigned int i=0; i<ncut; i++, ++itK)
      itK.Set( itK.Get() * TFFTPrecision(0.5*(1+vcl_cos(vnl_math::pi*i/ncut))));
    }
  else if(this->GetCosineCutFrequency() > 0.)
    {
    const unsigned int ncut = itk::Math::Round<double>(n * vnl_math_min(1.0, this->GetCosineCutFrequency() ) );
    for(unsigned int i=0; i<ncut; i++, ++itK)
      itK.Set( itK.Get() * TFFTPrecision(vcl_cos(0.5*vnl_math::pi*i/ncut)));
    }
  else if(this->GetHammingFrequency() > 0.)
    {
    const unsigned int ncut = itk::Math::Round<double>(n * vnl_math_min(1.0, this->GetHammingFrequency() ) );
    for(unsigned int i=0; i<ncut; i++, ++itK)
      itK.Set( itK.Get() * TFFTPrecision(0.54+0.46*(vcl_cos(vnl_math::pi*i/ncut))));
    }
  else
    {
    itK.GoToReverseBegin();
    ++itK;
    }

  for(; !itK.IsAtEnd(); ++itK)
    {
    // FFTW returns only the first half whereas vnl return the mirrored fft
#if ITK_VERSION_MAJOR <= 3
#  if !defined(USE_FFTWD)
    if(typeid(TFFTPrecision).name() == typeid(double).name() && (unsigned int)itK.GetIndex()[0]>n)
      {
      typename FFTType::OutputImageType::IndexType mirroredIndex = itK.GetIndex();
      mirroredIndex[0] = fftK->GetOutput()->GetLargestPossibleRegion().GetSize()[0] - mirroredIndex[0];
      itK.Set( fftK->GetOutput()->GetPixel(mirroredIndex) );
      }
    else
#  endif
#  if !defined(USE_FFTWF)
    if(typeid(TFFTPrecision).name() == typeid(float).name() && (unsigned int)itK.GetIndex()[0]>n)
      {
      typename FFTType::OutputImageType::IndexType mirroredIndex = itK.GetIndex();
      mirroredIndex[0] = fftK->GetOutput()->GetLargestPossibleRegion().GetSize()[0] - mirroredIndex[0];
      itK.Set( fftK->GetOutput()->GetPixel(mirroredIndex) );
      }
    else
#  endif
#endif
    itK.Set( itK.Get() * TFFTPrecision(0.) );
    }

  // Replicate and window if required
  typename TFFTOutputImage::Pointer result = fftK->GetOutput();
  if(this->GetHannCutFrequencyY()>0.)
    {
    size.Fill(1);
    size[0] = fftK->GetOutput()->GetLargestPossibleRegion().GetSize(0);
    size[1] = height;

    const unsigned int ncut = itk::Math::Round<double>( (height/2+1) * vnl_math_min(1.0, this->GetHannCutFrequencyY() ) );

    result = TFFTOutputImage::New();
    result->SetRegions( size );
    result->Allocate();
    result->FillBuffer(0.);

    IteratorType itTwoDK(result, result->GetLargestPossibleRegion() );
    for(unsigned int j=0; j<ncut; j++)
      {
      itK.GoToBegin();
      const TFFTPrecision win( 0.5*( 1+vcl_cos(vnl_math::pi*j/ncut) ) );
      for(unsigned int i=0; i<size[0]; ++itK, ++itTwoDK, i++)
        {
        itTwoDK.Set( win * itK.Get() );
        }
      }
    itTwoDK.GoToReverseBegin();
    for(unsigned int j=1; j<ncut; j++)
      {
      itK.GoToReverseBegin();
      const TFFTPrecision win( 0.5*( 1+vcl_cos(vnl_math::pi*j/ncut) ) );
      for(unsigned int i=0; i<size[0]; --itK, --itTwoDK, i++)
        {
        itTwoDK.Set( win * itK.Get() );
        }
      }
    }

  return result;
}

template<class TInputImage, class TOutputImage, class TFFTPrecision>
void
FFTRampImageFilter<TInputImage, TOutputImage, TFFTPrecision>
::UpdateTruncationMirrorWeights()
{
  const unsigned int next = this->GetTruncationCorrectionExtent();

  if ( (unsigned int) m_TruncationMirrorWeights.size() != next)
    {
    m_TruncationMirrorWeights.resize(next+1);
    for(unsigned int i=0; i<next+1; i++)
      m_TruncationMirrorWeights[i] = pow( sin( (next-i)*vnl_math::pi/(2*next-2) ), 0.75);
    }
}

} // end namespace rtk
#endif
