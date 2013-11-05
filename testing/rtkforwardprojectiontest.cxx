
#include "rtkTestConfiguration.h"
#include "rtkThreeDCircularProjectionGeometryXMLFile.h"
#include "rtkRayBoxIntersectionImageFilter.h"
#include "rtkSheppLoganPhantomFilter.h"
#include "rtkDrawSheppLoganFilter.h"
#include "rtkConstantImageSource.h"

#ifdef USE_CUDA
#  include "rtkCudaForwardProjectionImageFilter.h"
#else
#  include "rtkJosephForwardProjectionImageFilter.h"
#endif

template<class TImage1, class TImage2>
#if FAST_TESTS_NO_CHECKS
void CheckImageQuality(typename TImage1::Pointer itkNotUsed(recon), typename TImage2::Pointer itkNotUsed(ref))
{
}
#else
void CheckImageQuality(typename TImage1::Pointer recon, typename TImage2::Pointer ref)
{
  typedef itk::ImageRegionConstIterator<TImage1> ImageIteratorType1;
  typedef itk::ImageRegionConstIterator<TImage2> ImageIteratorType2;
  ImageIteratorType1 itTest( recon, recon->GetBufferedRegion() );
  ImageIteratorType2 itRef( ref, ref->GetBufferedRegion() );

  typedef double ErrorType;
  ErrorType TestError = 0.;
  ErrorType EnerError = 0.;

  itTest.GoToBegin();
  itRef.GoToBegin();

  while( !itRef.IsAtEnd() )
    {
    typename TImage1::PixelType TestVal = itTest.Get();
    typename TImage2::PixelType RefVal = itRef.Get();

    if( TestVal != RefVal )
      {
      TestError += vcl_abs(RefVal - TestVal);
      EnerError += vcl_pow(ErrorType(RefVal - TestVal), 2.);
      }
    ++itTest;
    ++itRef;
    }
  // Error per Pixel
  ErrorType ErrorPerPixel = TestError/recon->GetBufferedRegion().GetNumberOfPixels();
  std::cout << "\nError per Pixel = " << ErrorPerPixel << std::endl;
  // MSE
  ErrorType MSE = EnerError/ref->GetBufferedRegion().GetNumberOfPixels();
  std::cout << "MSE = " << MSE << std::endl;
  // PSNR
  ErrorType PSNR = 20*log10(255.0) - 10*log10(MSE);
  std::cout << "PSNR = " << PSNR << "dB" << std::endl;
  // QI
  ErrorType QI = (255.0-ErrorPerPixel)/255.0;
  std::cout << "QI = " << QI << std::endl;

  // Checking results
  if (ErrorPerPixel > 1.28)
    {
    std::cerr << "Test Failed, Error per pixel not valid! "
              << ErrorPerPixel << " instead of 1.28" << std::endl;
    //exit( EXIT_FAILURE);
    }
  if (PSNR < 44.)
    {
    std::cerr << "Test Failed, PSNR not valid! "
              << PSNR << " instead of 44" << std::endl;
    //exit( EXIT_FAILURE);
    }
}
#endif

int main(int , char** )
{
  const unsigned int Dimension = 3;
  typedef float                                    OutputPixelType;

#ifdef USE_CUDA
  typedef itk::CudaImage< OutputPixelType, Dimension > OutputImageType;
#else
  typedef itk::Image< OutputPixelType, Dimension > OutputImageType;
#endif

  typedef itk::Image< OutputPixelType, Dimension > OutputImageType2;

  typedef itk::Vector<double, 3>                   VectorType;
#if FAST_TESTS_NO_CHECKS
  const unsigned int NumberOfProjectionImages = 3;
#else
  const unsigned int NumberOfProjectionImages = 45;
#endif

  // Constant image sources
  typedef rtk::ConstantImageSource< OutputImageType > ConstantImageSourceType;
  ConstantImageSourceType::PointType origin;
  ConstantImageSourceType::SizeType size;
  ConstantImageSourceType::SpacingType spacing;

  // The test projects a volume filled with ones. The forward projector should
  // then return the intersection of the ray with the box and it is compared
  // with the analytical intersection of a box with a ray.

  // Create Joseph Forward Projector volume input.
  const ConstantImageSourceType::Pointer volInput = ConstantImageSourceType::New();
  origin[0] = -126;
  origin[1] = -126;
  origin[2] = -126;
#if FAST_TESTS_NO_CHECKS
  size[0] = 2;
  size[1] = 2;
  size[2] = 2;
  spacing[0] = 252.;
  spacing[1] = 252.;
  spacing[2] = 252.;
#else
  size[0] = 64;
  size[1] = 64;
  size[2] = 64;
  spacing[0] = 4.;
  spacing[1] = 4.;
  spacing[2] = 4.;
#endif
  volInput->SetOrigin( origin );
  volInput->SetSpacing( spacing );
  volInput->SetSize( size );
  volInput->SetConstant( 1. );
  volInput->UpdateOutputInformation();

  // Initialization Volume, it is used in the Joseph Forward Projector and in the
  // Ray Box Intersection Filter in order to initialize the stack of projections.
  const ConstantImageSourceType::Pointer projInput = ConstantImageSourceType::New();
  size[2] = NumberOfProjectionImages;
  projInput->SetOrigin( origin );
  projInput->SetSpacing( spacing );
  projInput->SetSize( size );
  projInput->SetConstant( 0. );
  projInput->Update();

  // Joseph Forward Projection filter
#ifdef USE_CUDA
  typedef rtk::CudaForwardProjectionImageFilter JFPType;
#else
  typedef rtk::JosephForwardProjectionImageFilter<OutputImageType, OutputImageType> JFPType;
#endif
  JFPType::Pointer jfp = JFPType::New();
  jfp->InPlaceOff();
  jfp->SetInput( projInput->GetOutput() );
  jfp->SetInput( 1, volInput->GetOutput() );

  // Ray Box Intersection filter (reference)
  typedef rtk::RayBoxIntersectionImageFilter<OutputImageType2, OutputImageType2> RBIType;
  RBIType::Pointer rbi = RBIType::New();
  rbi->InPlaceOff();
  rbi->SetInput( projInput->GetOutput() );
  VectorType boxMin, boxMax;
  boxMin[0] = -126.0;
  boxMin[1] = -126.0;
  boxMin[2] = -126.0;
  boxMax[0] =  126.0;
  boxMax[1] =  126.0;
  boxMax[2] =   47.6;
  rbi->SetBoxMin(boxMin);
  rbi->SetBoxMax(boxMax);

  std::cout << "\n\n****** Case 1: inner ray source ******" << std::endl;
  // The circle is divided in 4 quarters
  for(int q=0; q<4; q++) {
    // Geometry
    typedef rtk::ThreeDCircularProjectionGeometry GeometryType;
    GeometryType::Pointer geometry = GeometryType::New();
    for(unsigned int i=0; i<NumberOfProjectionImages;i++)
      {
      const double angle = -45. + i*2.;
      geometry->AddProjection(47.6 / vcl_cos(angle*itk::Math::pi/180.), 1000., q*90+angle);
      }

    if(q==0) {
      rbi->SetGeometry( geometry );
      rbi->Update();
    }

    jfp->SetGeometry(geometry);
    jfp->Update();

    CheckImageQuality<OutputImageType2, OutputImageType>(rbi->GetOutput(), jfp->GetOutput());
    std::cout << "\n\nTest of quarter #" << q << " PASSED! " << std::endl;
  }

  std::cout << "\n\n****** Case 2: outer ray source ******" << std::endl;
  boxMax[2] = 126.0;
  rbi->SetBoxMax(boxMax);

  // Geometry
  typedef rtk::ThreeDCircularProjectionGeometry GeometryType;
  GeometryType::Pointer geometry = GeometryType::New();
  for(unsigned int i=0; i<NumberOfProjectionImages; i++)
    geometry->AddProjection(500., 1000., i*8.);

  rbi->SetGeometry( geometry );
  rbi->Update();

  jfp->SetGeometry( geometry );
  jfp->Update();

  CheckImageQuality<OutputImageType2, OutputImageType>(rbi->GetOutput(), jfp->GetOutput());
  std::cout << "\n\nTest PASSED! " << std::endl;

  std::cout << "\n\n****** Case 3: Shepp-Logan, outer ray source ******" << std::endl;

  // Create Shepp Logan reference projections
  typedef rtk::SheppLoganPhantomFilter<OutputImageType2, OutputImageType2> SLPType;
  SLPType::Pointer slp = SLPType::New();
  slp->InPlaceOff();
  slp->SetInput( projInput->GetOutput() );
  slp->SetGeometry(geometry);
  slp->Update();

  // Create a Shepp Logan reference volume (finer resolution)
  origin.Fill(-127);
  size.Fill(128);
  spacing.Fill(2.);
  volInput->SetOrigin( origin );
  volInput->SetSpacing( spacing );
  volInput->SetSize( size );
  volInput->SetConstant( 0. );

  typedef rtk::DrawSheppLoganFilter<OutputImageType2, OutputImageType> DSLType;
  DSLType::Pointer dsl = DSLType::New();
  dsl->InPlaceOff();
  dsl->SetInput( volInput->GetOutput() );
  dsl->Update();
  
  // Forward projection
  jfp->SetInput( 1, dsl->GetOutput() );
  jfp->Update();

  CheckImageQuality<OutputImageType2, OutputImageType>(slp->GetOutput(), jfp->GetOutput());
  std::cout << "\n\nTest PASSED! " << std::endl;

  std::cout << "\n\n****** Case 4: Shepp-Logan, inner ray source ******" << std::endl;
  geometry = GeometryType::New();
  for(unsigned int i=0; i<NumberOfProjectionImages; i++)
    geometry->AddProjection(120., 1000., i*8.);

  slp->SetGeometry(geometry);
  slp->Update();

  jfp->SetGeometry( geometry );
  jfp->Update();

  CheckImageQuality<OutputImageType2, OutputImageType>(slp->GetOutput(), jfp->GetOutput());
  std::cout << "\n\nTest PASSED! " << std::endl;

  return EXIT_SUCCESS;
}
