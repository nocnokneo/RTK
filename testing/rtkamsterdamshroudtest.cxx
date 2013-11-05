#include "rtkTestConfiguration.h"

#include <itkImageFileReader.h>
#include <itkPasteImageFilter.h>

#include "rtkAmsterdamShroudImageFilter.h"
#include "rtkConstantImageSource.h"
#include "rtkTestConfiguration.h"
#include "rtkThreeDCircularProjectionGeometryXMLFile.h"
#include "rtkRayEllipsoidIntersectionImageFilter.h"
#include "rtkConfiguration.h"
#include "rtkReg1DExtractShroudSignalImageFilter.h"
#include "rtkDPExtractShroudSignalImageFilter.h"

template<class TInputImage>
#if FAST_TESTS_NO_CHECKS
void CheckImageQuality(typename TInputImage::Pointer itkNotUsed(recon), typename TInputImage::Pointer itkNotUsed(ref))
{
}
#else
void CheckImageQuality(typename TInputImage::Pointer recon, typename TInputImage::Pointer ref)
{
  typedef itk::ImageRegionConstIterator<TInputImage>  ImageIteratorInType;
  ImageIteratorInType itTest( recon, recon->GetBufferedRegion() );
  ImageIteratorInType itRef( ref, ref->GetBufferedRegion() );

  typedef double ErrorType;
  ErrorType TestError = 0.;
  ErrorType EnerError = 0.;

  itTest.GoToBegin();
  itRef.GoToBegin();

  while( !itRef.IsAtEnd() )
    {
    typename TInputImage::PixelType TestVal = itTest.Get();
    typename TInputImage::PixelType RefVal  = itRef.Get();

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
  ErrorType PSNR = 20*log10(6304.0) - 10*log10(MSE);
  std::cout << "PSNR = " << PSNR << "dB" << std::endl;
  // QI
  ErrorType QI = (6304.-ErrorPerPixel)/6304.;
  std::cout << "QI = " << QI << std::endl;

  // Checking results
  if (ErrorPerPixel > 1.20e-6)
  {
    std::cerr << "Test Failed, Error per pixel not valid! "
              << ErrorPerPixel << " instead of 1.20e-6." << std::endl;
    exit( EXIT_FAILURE);
  }
  if (PSNR < 185.)
  {
    std::cerr << "Test Failed, PSNR not valid! "
              << PSNR << " instead of 185" << std::endl;
    exit( EXIT_FAILURE);
  }
}
#endif

/**
 * \file rtkamsterdamshroudtest.cxx
 *
 * \brief Functional test for classes performing Amsterdam Shroud and breathing signal extraction algorithms
 *
 * This test generates an Amsterdam Shroud image from a moving simulated phantom
 * and extracts the breathing signal using two different methods, reg1D and D
 * algorithms. The generated results are compared to the expected results,
 * read from a baseline image in the MetaIO file format and hard-coded,
 * respectively.
 *
 * \author Marc Vila
 */
int main(int, char** )
{
  const unsigned int Dimension = 3;
  typedef double                                     reg1DPixelType;
  typedef float                                      OutputPixelType;
  typedef itk::Image< OutputPixelType, Dimension >   OutputImageType;
  typedef itk::Image< reg1DPixelType, Dimension-2 >  reg1DImageType;
#if FAST_TESTS_NO_CHECKS
  const unsigned int NumberOfProjectionImages = 3;
#else
  const unsigned int NumberOfProjectionImages = 100;
#endif

  typedef rtk::ThreeDCircularProjectionGeometry GeometryType;
  GeometryType::Pointer geometryMain = GeometryType::New();
  for(unsigned int noProj=0; noProj<NumberOfProjectionImages; noProj++)
    geometryMain->AddProjection(600., 1200., noProj*360./NumberOfProjectionImages);

  // Create a stack of empty projection images
  typedef rtk::ConstantImageSource< OutputImageType > ConstantImageSourceType;
  ConstantImageSourceType::PointType origin;
  ConstantImageSourceType::SizeType sizeOutput;
  ConstantImageSourceType::SpacingType spacing;

  // Adjust size according to geometry and for just one projection
  ConstantImageSourceType::Pointer constantImageSourceSingleProjection = ConstantImageSourceType::New();
  origin[0] = -50.;
  origin[1] = -50.;
  origin[2] = -158.75;
#if FAST_TESTS_NO_CHECKS
  sizeOutput[0] = 4;
  sizeOutput[1] = 4;
  sizeOutput[2] = 1;
  spacing[0] = 106.;
  spacing[1] = 106.;
  spacing[2] = 2.5;
#else
  sizeOutput[0] = 128;
  sizeOutput[1] = 128;
  sizeOutput[2] = 1;
  spacing[0] = 2.5;
  spacing[1] = 2.5;
  spacing[2] = 2.5;
#endif
  constantImageSourceSingleProjection->SetOrigin( origin );
  constantImageSourceSingleProjection->SetSpacing( spacing );
  constantImageSourceSingleProjection->SetSize( sizeOutput );
  constantImageSourceSingleProjection->SetConstant( 0. );

  // Adjust size according to geometry
  ConstantImageSourceType::Pointer constantImageSource = ConstantImageSourceType::New();
  sizeOutput[2] = NumberOfProjectionImages;
  constantImageSource->SetOrigin( origin );
  constantImageSource->SetSpacing( spacing );
  constantImageSource->SetSize( sizeOutput );
  constantImageSource->SetConstant( 0. );

  typedef itk::PasteImageFilter <OutputImageType, OutputImageType, OutputImageType > PasteImageFilterType;
  OutputImageType::IndexType destinationIndex;
  destinationIndex[0] = 0;
  destinationIndex[1] = 0;
  destinationIndex[2] = 0;

  PasteImageFilterType::Pointer pasteFilter = PasteImageFilterType::New();

  // Single projection
  typedef rtk::RayEllipsoidIntersectionImageFilter<OutputImageType, OutputImageType> REIType;
  double             size   = 80.;
  double             sinus  = 0.;
  const unsigned int Cycles = 4;

  OutputImageType::Pointer wholeImage = constantImageSource->GetOutput();
  for (unsigned int i=1; i <= sizeOutput[2]; i++)
  {
    // Geometry object
    GeometryType::Pointer geometry = GeometryType::New();
    geometry->AddProjection(1200., 1500., i*360/sizeOutput[2]);

    // Ellipse 1
    REIType::Pointer e1 = REIType::New();
    REIType::VectorType semiprincipalaxis, center;
    semiprincipalaxis[0] = 88.32;
    semiprincipalaxis[1] = 115.2;
    semiprincipalaxis[2] = 117.76;
    center[0] = 0.;
    center[1] = 0.;
    center[2] = 0.;
    e1->SetInput(constantImageSourceSingleProjection->GetOutput());
    e1->SetGeometry(geometry);
    e1->SetDensity(2.);
    e1->SetAxis(semiprincipalaxis);
    e1->SetCenter(center);
    e1->SetAngle(0.);
    e1->InPlaceOff();
    e1->Update();

    // Ellipse 2
    REIType::Pointer e2 = REIType::New();
    semiprincipalaxis[0] = 35.;
    semiprincipalaxis[1] = size - sinus;
    semiprincipalaxis[2] = size - sinus;
    center[0] = -37.;
    center[1] = 0.;
    center[2] = 0.;
    e2->SetInput(e1->GetOutput());
    e2->SetGeometry(geometry);
    e2->SetDensity(-1.98);
    e2->SetAxis(semiprincipalaxis);
    e2->SetCenter(center);
    e2->SetAngle(0.);
    e2->Update();

    // Ellipse 3
    REIType::Pointer e3 = REIType::New();
    semiprincipalaxis[0] = 35.;
    semiprincipalaxis[1] = size - sinus;
    semiprincipalaxis[2] = size - sinus;
    center[0] = 37.;
    center[1] = 0.;
    center[2] = 0.;
    e3->SetInput(e2->GetOutput());
    e3->SetGeometry(geometry);
    e3->SetDensity(-1.98);
    e3->SetAxis(semiprincipalaxis);
    e3->SetCenter(center);
    e3->SetAngle(0.);
    e3->Update();

    // Ellipse 4
    REIType::Pointer e4 = REIType::New();
    semiprincipalaxis[0] = 8.;
    semiprincipalaxis[1] = 8.;
    semiprincipalaxis[2] = 8.;
    center[0] = -40.;
    center[1] = 0.;
    center[2] = 0.;
    e4->SetInput(e3->GetOutput());
    e4->SetGeometry(geometry);
    e4->SetDensity(1.42);
    e4->SetAxis(semiprincipalaxis);
    e4->SetCenter(center);
    e4->SetAngle(0.);

    // Creating movement
    sinus = 15*sin( i*2*itk::Math::pi/(sizeOutput[2]/Cycles) );

    // Generating projection
    e4->Update();

    // Adding each projection to volume
    pasteFilter->SetSourceImage(e4->GetOutput());
    pasteFilter->SetDestinationImage(wholeImage);
    pasteFilter->SetSourceRegion(e4->GetOutput()->GetLargestPossibleRegion());
    pasteFilter->SetDestinationIndex(destinationIndex);
    pasteFilter->Update();
    wholeImage = pasteFilter->GetOutput();
    destinationIndex[2]++;
  }

  std::cout << "\n\n****** Case 1: Amsterdam Shroud Image ******" << std::endl;

  // Amsterdam shroud
  typedef rtk::AmsterdamShroudImageFilter<OutputImageType> shroudFilterType;
  shroudFilterType::Pointer shroudFilter = shroudFilterType::New();
  shroudFilter->SetInput( pasteFilter->GetOutput() );
  TRY_AND_EXIT_ON_ITK_EXCEPTION(shroudFilter->Update());

  // Read reference object
  typedef itk::ImageFileReader< shroudFilterType::OutputImageType > ReaderAmsterdamType;
  ReaderAmsterdamType::Pointer reader2 = ReaderAmsterdamType::New();
  reader2->SetFileName(std::string(RTK_DATA_ROOT) +
                       std::string("/Baseline/AmsterdamShroud/Amsterdam.mha"));
  reader2->Update();

  CheckImageQuality< shroudFilterType::OutputImageType >(shroudFilter->GetOutput(), reader2->GetOutput());
  std::cout << "Test PASSED! " << std::endl;

  std::cout << "\n\n****** Case 2: Breathing signal calculated by reg1D algorithm ******\n" << std::endl;

  //Estimation of breathing signal
  typedef rtk::Reg1DExtractShroudSignalImageFilter< reg1DPixelType, reg1DPixelType > reg1DFilterType;
  reg1DImageType::Pointer  reg1DSignal;
  reg1DFilterType::Pointer reg1DFilter = reg1DFilterType::New();
  reg1DFilter->SetInput( reader2->GetOutput() );
  reg1DFilter->Update();
  reg1DSignal = reg1DFilter->GetOutput();

#if !(FAST_TESTS_NO_CHECKS)
  //Test Reference
  float reg1D[100] = {0, 4.5, 8.625, 12.25, 15, 16.875, 17.625, 17.375, 16.125, 13.875, 10.75, 7.125,
                      3, -1.25, -5.375, -9, -12.125, -14.25, -15.625, -16.125, -15.5, -13.75, -11, -7.5,
                      -3.375, 1.125, 5.5, 9.5, 13, 15.875, 17.75, 18.625, 18.375, 17.25, 15, 11.875,
                      8.125, 3.875, -0.625, -5, -8.875, -12.125, -14.25, -15.375, -15.375, -14.625,
                      -12.875, -10.25, -6.875, -2.75, 1.625, 6, 10.125, 13.625, 16.375, 18.25, 19,
                      18.75, 17.5, 15.25, 12.125, 8.5, 4.375, 0.125, -4, -7.625, -10.75, -12.875,
                      -14.25, -14.75, -14.125, -12.375, -9.625, -6.125, -2, 2.5, 6.875, 10.875,
                      14.375, 17.25, 19.125, 20, 19.75, 18.625, 16.375, 13.125, 9.375, 5.125,
                      0.625, -3.75, -7.625, -10.875, -13, -14.125, -14.125, -13.375, -11.5,
                      -8.875, -5.375, -1.25};

  //Checking for possible errors
  float zeroValue = 1e-12;
  float sum = 0.;
  unsigned int i = 0;
  itk::ImageRegionConstIterator<reg1DImageType> it( reg1DSignal, reg1DSignal->GetLargestPossibleRegion() );
  for (it.GoToBegin(); !it.IsAtEnd(); ++it, i++)
    sum += vcl_abs(reg1D[i] - it.Get());

  if ( sum <= zeroValue )
    std::cout << "Test PASSED! " << std::endl;
  else
  {
    std::cerr << "Test FAILED! " << "Breathing signal does not match, absolute difference " << sum << " instead of 0." << std::endl;
    exit( EXIT_FAILURE);
  }
#endif

  std::cout << "\n\n****** Case 3: Breathing signal calculated by DP algorithm ******\n" << std::endl;

  //Estimation of breathing signal
  typedef rtk::DPExtractShroudSignalImageFilter< reg1DPixelType, reg1DPixelType > DPFilterType;
  reg1DImageType::Pointer  DPSignal;
  DPFilterType::Pointer DPFilter = DPFilterType::New();
  DPFilter->SetInput( reader2->GetOutput() );
  DPFilter->SetAmplitude( 20. );
  DPFilter->Update();
  DPSignal = DPFilter->GetOutput();

#if !(FAST_TESTS_NO_CHECKS)
  //Test Reference
  float DP[100] = {2.5, 7.5, 12.5, 15, 17.5, 20, 20, 20, 20, 17.5, 12.5, 10, 5, 0, -5, -7.5, -10,
                   -12.5, -15, -15, -15, -12.5, -10, -7.5, -2.5, 2.5, 7.5, 10, 15, 17.5, 20, 20,
                   20, 17.5, 15, 12.5, 10, 5, 0, -5, -7.5, -10, -12.5, -15, -15, -12.5, -12.5,
                   -10, -5, 0, 2.5, 7.5, 12.5, 15, 17.5, 20, 20, 20, 20, 17.5, 12.5, 10, 5, 0,
                   -5, -7.5, -10, -12.5, -15, -15, -15, -12.5, -10, -7.5, -2.5, 2.5, 7.5, 10,
                   15, 17.5, 20, 20, 20, 17.5, 15, 12.5, 10, 5, 0, -5, -7.5, -10, -12.5, -15, -15,
                   -12.5, -12.5, -10, -5, 0};

  //Checking for possible errors
  sum = 0.;
  i = 0;
  itk::ImageRegionConstIterator< reg1DImageType > itDP( DPSignal, DPSignal->GetLargestPossibleRegion() );
  for (itDP.GoToBegin(); !itDP.IsAtEnd(); ++itDP, i++)
    sum += vcl_abs(DP[i] - itDP.Get());

  if ( sum <= zeroValue )
    std::cout << "Test PASSED! " << std::endl;
  else
  {
    std::cerr << "Test FAILED! " << "Breathing signal does not match, absolute difference " << sum << " instead of 0." << std::endl;
    exit( EXIT_FAILURE);
  }
#endif

  return EXIT_SUCCESS;
}
