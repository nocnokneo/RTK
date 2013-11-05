
#ifdef RAMP_FILTER_TEST_WITHOUT_FFTW
#  include "rtkConfiguration.h"
#  include <itkImageToImageFilter.h>
#  if defined(ITK_USE_FFTWF)
#    undef ITK_USE_FFTWF
#  endif
#  if defined(ITK_USE_FFTWD)
#    undef ITK_USE_FFTWD
#  endif
#  if defined(USE_FFTWF)
#    undef USE_FFTWF
#  endif
#  if defined(USE_FFTWD)
#    undef USE_FFTWD
#  endif
#  include "rtkFFTRampImageFilter.h"
#endif

#include <itkImageRegionConstIterator.h>

#include "rtkTestConfiguration.h"
#include "rtkSheppLoganPhantomFilter.h"
#include "rtkDrawSheppLoganFilter.h"
#include "rtkConstantImageSource.h"
#include "rtkAdditiveGaussianNoiseImageFilter.h"

#ifdef USE_CUDA
#  include "rtkCudaFDKConeBeamReconstructionFilter.h"
#else
#  include "rtkFDKConeBeamReconstructionFilter.h"
#endif

template<class TImage>
#if FAST_TESTS_NO_CHECKS
void CheckImageQuality(typename TImage::Pointer itkNotUsed(recon),
                       typename TImage::Pointer itkNotUsed(ref),
                       double itkNotUsed(refLowerThreshold),
                       double itkNotUsed(refUpperThreshold),
                       double itkNotUsed(snrThreshold),
                       double itkNotUsed(errorPerPixelThreshold))
{
}
#else
void CheckImageQuality(typename TImage::Pointer recon,
                       typename TImage::Pointer ref,
                       double refLowerThreshold,
                       double refUpperThreshold,
                       double snrThreshold,
                       double errorPerPixelThreshold)
{
  typedef itk::ImageRegionConstIterator<TImage> ImageIteratorType;
  ImageIteratorType itTest( recon, recon->GetBufferedRegion() );
  ImageIteratorType itRef( ref, ref->GetBufferedRegion() );

  typedef double ErrorType;
  ErrorType TestError = 0.;
  ErrorType EnerError = 0.;

  itTest.GoToBegin();
  itRef.GoToBegin();

  unsigned int npix=0;
  while( !itRef.IsAtEnd() )
    {
    typename TImage::PixelType testVal = itTest.Get();
    typename TImage::PixelType refVal = itRef.Get();
    if( testVal != refVal && (refVal>=refLowerThreshold && refVal<=refUpperThreshold) )
      {
        TestError += vcl_abs(refVal - testVal);
        EnerError += vcl_pow(ErrorType(refVal - testVal), 2.);
        npix++;
      }
    ++itTest;
    ++itRef;
    }
  // Error per Pixel
  ErrorType ErrorPerPixel = TestError/npix;
  std::cout << "\nError per Pixel = " << ErrorPerPixel << std::endl;
  // MSE
  ErrorType MSE = EnerError/npix;
  std::cout << "MSE = " << MSE << std::endl;
  // PSNR
  ErrorType PSNR = 20*log10(2.0) - 10*log10(MSE);
  std::cout << "PSNR = " << PSNR << "dB" << std::endl;

  // Checking results
  if (ErrorPerPixel > errorPerPixelThreshold)
  {
    std::cerr << "Test Failed, Error per pixel not valid! "
              << ErrorPerPixel << " instead of " << errorPerPixelThreshold << std::endl;
    exit( EXIT_FAILURE);
  }
  if (PSNR < snrThreshold)
  {
    std::cerr << "Test Failed, PSNR not valid! "
              << PSNR << " instead of " << snrThreshold << std::endl;
    exit( EXIT_FAILURE);
  }
}
#endif

/**
 * \file rtkrampfiltertest.cxx
 *
 * \brief Functional test for the ramp filter of the FDK reconstruction.
 *
 * This test generates the projections of a simulated Shepp-Logan phantom in
 * different reconstruction scenarios (noise, truncation).
 * CT images are reconstructed from each set of projection images using the
 * FDK algorithm with different configuration of the ramp filter in order to
 * reduce the possible artifacts. The generated results are compared to the
 * expected results (analytical calculation).
 *
 * \author Simon Rit
 */

int main(int , char** )
{
  const unsigned int Dimension = 3;
  typedef float                                    OutputPixelType;
#ifdef USE_CUDA
  typedef itk::CudaImage< OutputPixelType, Dimension > OutputImageType;
#else
  typedef itk::Image< OutputPixelType, Dimension > OutputImageType;
#endif
#if FAST_TESTS_NO_CHECKS
  const unsigned int NumberOfProjectionImages = 3;
#else
  const unsigned int NumberOfProjectionImages = 180;
#endif

  // Constant image sources
  typedef rtk::ConstantImageSource< OutputImageType > ConstantImageSourceType;
  ConstantImageSourceType::PointType origin;
  ConstantImageSourceType::SizeType size;
  ConstantImageSourceType::SpacingType spacing;

  ConstantImageSourceType::Pointer tomographySource  = ConstantImageSourceType::New();
  origin[0] = -127.;
  origin[1] = -127.;
  origin[2] = -127.;
#if FAST_TESTS_NO_CHECKS
  size[0] = 2;
  size[1] = 2;
  size[2] = 2;
  spacing[0] = 254.;
  spacing[1] = 254.;
  spacing[2] = 254.;
#else
  size[0] = 128;
  size[1] = 128;
  size[2] = 128;
  spacing[0] = 2.;
  spacing[1] = 2.;
  spacing[2] = 2.;
#endif
  tomographySource->SetOrigin( origin );
  tomographySource->SetSpacing( spacing );
  tomographySource->SetSize( size );
  tomographySource->SetConstant( 0. );

  ConstantImageSourceType::Pointer projectionsSource = ConstantImageSourceType::New();
  origin[0] = -254.;
  origin[1] = -254.;
  origin[2] = -254.;
#if FAST_TESTS_NO_CHECKS
  size[0] = 2;
  size[1] = 2;
  size[2] = NumberOfProjectionImages;
  spacing[0] = 508.;
  spacing[1] = 508.;
  spacing[2] = 508.;
#else
  size[0] = 128;
  size[1] = 128;
  size[2] = NumberOfProjectionImages;
  spacing[0] = 4.;
  spacing[1] = 4.;
  spacing[2] = 4.;
#endif
  projectionsSource->SetOrigin( origin );
  projectionsSource->SetSpacing( spacing );
  projectionsSource->SetSize( size );
  projectionsSource->SetConstant( 0. );

  // Geometry object
  typedef rtk::ThreeDCircularProjectionGeometry GeometryType;
  GeometryType::Pointer geometry = GeometryType::New();
  for(unsigned int noProj=0; noProj<NumberOfProjectionImages; noProj++)
    geometry->AddProjection(600., 1200., noProj*360./NumberOfProjectionImages);

  // Shepp Logan projections filter
  typedef rtk::SheppLoganPhantomFilter<OutputImageType, OutputImageType> SLPType;
  SLPType::Pointer slp=SLPType::New();
  slp->SetInput( projectionsSource->GetOutput() );
  slp->SetGeometry(geometry);

  std::cout << "\n\n****** Test 1: add noise and test Hann window ******" << std::endl;

  // Add noise
  typedef rtk::AdditiveGaussianNoiseImageFilter< OutputImageType > NIFType;
  NIFType::Pointer noisy=NIFType::New();
  noisy->SetInput( slp->GetOutput() );
  noisy->SetMean( 0.0 );
  noisy->SetStandardDeviation( 1. );

  // Create a reference object (in this case a 3D phantom reference).
  typedef rtk::DrawSheppLoganFilter<OutputImageType, OutputImageType> DSLType;
  DSLType::Pointer dsl = DSLType::New();
  dsl->SetInput( tomographySource->GetOutput() );
  TRY_AND_EXIT_ON_ITK_EXCEPTION( dsl->Update() );

  // FDK reconstruction filtering
#ifdef USE_CUDA
  typedef rtk::CudaFDKConeBeamReconstructionFilter                FDKType;
#else
  typedef rtk::FDKConeBeamReconstructionFilter< OutputImageType > FDKType;
#endif
  FDKType::Pointer feldkamp = FDKType::New();
  feldkamp->SetInput( 0, tomographySource->GetOutput() );
  feldkamp->SetInput( 1, noisy->GetOutput() );
  feldkamp->SetGeometry( geometry );
  feldkamp->GetRampFilter()->SetHannCutFrequency(0.8);
  TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkamp->Update() );

  CheckImageQuality<OutputImageType>(feldkamp->GetOutput(), dsl->GetOutput(), 1.05, 1.06, 40, 0.13);

  std::cout << "\n\n****** Test 1.5: add noise and test HannY window ******" << std::endl;
  feldkamp->GetRampFilter()->SetHannCutFrequencyY(0.8);
  feldkamp->Modified();
  TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkamp->Update() );
  CheckImageQuality<OutputImageType>(feldkamp->GetOutput(), dsl->GetOutput(), 1.05, 1.06, 40, 0.13);

  std::cout << "\n\n****** Test 2: smaller detector and test data padding for truncation ******" << std::endl;

  size[0] = 114;
  projectionsSource->SetSize( size );
  TRY_AND_EXIT_ON_ITK_EXCEPTION( slp->UpdateLargestPossibleRegion() );

  FDKType::Pointer feldkampCropped = FDKType::New();
  feldkampCropped->SetInput( 0, tomographySource->GetOutput() );
  feldkampCropped->SetInput( 1, slp->GetOutput() );
  feldkampCropped->SetGeometry( geometry );
  feldkampCropped->GetRampFilter()->SetTruncationCorrection(0.1);
  TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCropped->Update() );

  CheckImageQuality<OutputImageType>(feldkampCropped->GetOutput(), dsl->GetOutput(), 1.015, 1.025, 26, 0.05);

  std::cout << "\n\nTest PASSED! " << std::endl;
  return EXIT_SUCCESS;
}
