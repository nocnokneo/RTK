#include <itkImageRegionConstIterator.h>
#include <itkBinaryThresholdImageFilter.h>

#include "rtkTestConfiguration.h"
#include "rtkMacro.h"
#include "rtkFieldOfViewImageFilter.h"
#include "rtkConstantImageSource.h"
#include "rtkBackProjectionImageFilter.h"

template<class TImage>
#if FAST_TESTS_NO_CHECKS
void CheckImageQuality(typename TImage::Pointer itkNotUsed(recon), typename TImage::Pointer itkNotUsed(ref))
{
}
#else
void CheckImageQuality(typename TImage::Pointer recon, typename TImage::Pointer ref)
{
  typedef itk::ImageRegionConstIterator<TImage> ImageIteratorType;
  ImageIteratorType itTest( recon, recon->GetBufferedRegion() );
  ImageIteratorType itRef( ref, ref->GetBufferedRegion() );

  typedef double ErrorType;
  ErrorType TestError = 0.;
  ErrorType EnerError = 0.;

  itTest.GoToBegin();
  itRef.GoToBegin();

  while( !itRef.IsAtEnd() )
    {
    typename TImage::PixelType TestVal = itTest.Get();
    typename TImage::PixelType RefVal = itRef.Get();
    TestError += vcl_abs(RefVal - TestVal);
    EnerError += vcl_pow(ErrorType(RefVal - TestVal), 2.);
    ++itTest;
    ++itRef;
    }
  // Error per Pixel
  ErrorType ErrorPerPixel = TestError/ref->GetBufferedRegion().GetNumberOfPixels();
  std::cout << "\nError per Pixel = " << ErrorPerPixel << std::endl;
  // MSE
  ErrorType MSE = EnerError/ref->GetBufferedRegion().GetNumberOfPixels();
  std::cout << "MSE = " << MSE << std::endl;
  // PSNR
  ErrorType PSNR = 20*log10(2.0) - 10*log10(MSE);
  std::cout << "PSNR = " << PSNR << "dB" << std::endl;
  // QI
  ErrorType QI = (2.0-ErrorPerPixel)/2.0;
  std::cout << "QI = " << QI << std::endl;

  // Checking results
  if (ErrorPerPixel > 0.02)
  {
    std::cerr << "Test Failed, Error per pixel not valid! "
              << ErrorPerPixel << " instead of 0.02." << std::endl;
    exit( EXIT_FAILURE);
  }
  if (PSNR < 23.5)
  {
    std::cerr << "Test Failed, PSNR not valid! "
              << PSNR << " instead of 23.5" << std::endl;
    exit( EXIT_FAILURE);
  }
}
#endif

/**
 * \file rtkfovtest.cxx
 *
 * \brief Functional test for classes in charge of creating a FOV (Field Of
 * View) mask
 *
 * This test generates a FOV mask that can be used after a reconstruction.
 * The generated results are compared to the expected results, which are
 * created with a threshold in the backprojection images of the volume.
 *
 * \author Simon Rit and Marc Vila
 */

int main(int , char** )
{
  const unsigned int Dimension = 3;
  typedef float                                    OutputPixelType;
  typedef itk::Image< OutputPixelType, Dimension > OutputImageType;
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

  // FOV filter Input Volume, it is used as the input to create the fov mask.
  ConstantImageSourceType::Pointer fovInput = ConstantImageSourceType::New();
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

  
  fovInput->SetOrigin( origin );
  fovInput->SetSpacing( spacing );
  fovInput->SetSize( size );
  fovInput->SetConstant( 1. );

  // BP volume
  const ConstantImageSourceType::Pointer bpInput = ConstantImageSourceType::New();
  bpInput->SetOrigin( origin );
  bpInput->SetSpacing( spacing );
  bpInput->SetSize( size );

  // BackProjection Input Projections, it is used as the input to create the fov mask.
  const ConstantImageSourceType::Pointer projectionsSource = ConstantImageSourceType::New();
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
  projectionsSource->SetConstant( 1. );

  std::cout << "\n\n****** Case 1: centered detector ******" << std::endl;

  // Geometry
  typedef rtk::ThreeDCircularProjectionGeometry GeometryType;
  GeometryType::Pointer geometry = GeometryType::New();
  for(unsigned int noProj=0; noProj<NumberOfProjectionImages; noProj++)
    geometry->AddProjection(600, 1200., noProj*360./NumberOfProjectionImages);

  // FOV
  typedef rtk::FieldOfViewImageFilter<OutputImageType, OutputImageType> FOVFilterType;
  FOVFilterType::Pointer fov=FOVFilterType::New();
  fov->SetInput(0, fovInput->GetOutput());
  fov->SetProjectionsStack(projectionsSource->GetOutput());
  fov->SetGeometry( geometry );
  fov->Update();

  // Backprojection reconstruction filter
  typedef rtk::BackProjectionImageFilter< OutputImageType, OutputImageType > BPType;
  BPType::Pointer bp = BPType::New();
  bp->SetInput( 0, bpInput->GetOutput() );
  bp->SetInput( 1, projectionsSource->GetOutput() );
  bp->SetGeometry( geometry.GetPointer() );

  // Thresholded to the number of projections
  typedef itk::BinaryThresholdImageFilter< OutputImageType, OutputImageType > ThresholdType;
  ThresholdType::Pointer threshold = ThresholdType::New();
  threshold->SetInput(bp->GetOutput());
  threshold->SetOutsideValue(0.);
  threshold->SetLowerThreshold(NumberOfProjectionImages-0.5);
  threshold->SetUpperThreshold(NumberOfProjectionImages+0.5);
  threshold->SetInsideValue(1.);
  TRY_AND_EXIT_ON_ITK_EXCEPTION( threshold->Update() );

  CheckImageQuality<OutputImageType>(fov->GetOutput(), threshold->GetOutput());
  std::cout << "\n\nTest PASSED! " << std::endl;

  std::cout << "\n\n****** Case 2: offset detector ******" << std::endl;

  origin[0] = -54.;
  projectionsSource->SetOrigin( origin );
  size[0] = 78;
  projectionsSource->SetSize( size );
  projectionsSource->UpdateOutputInformation();
  projectionsSource->UpdateLargestPossibleRegion();
  fov->SetDisplacedDetector(true);
  fov->Update();

  CheckImageQuality<OutputImageType>(fov->GetOutput(), threshold->GetOutput());
  std::cout << "\n\nTest PASSED! " << std::endl;
  
  return EXIT_SUCCESS;
}
