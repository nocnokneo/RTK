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

#include "rtkfdk_ggo.h"
#include "rtkGgoFunctions.h"
#include "rtkConfiguration.h"

#include "rtkThreeDCircularProjectionGeometryXMLFile.h"
#include "rtkProjectionsReader.h"
#include "rtkDisplacedDetectorImageFilter.h"
#include "rtkParkerShortScanImageFilter.h"
#include "rtkFDKConeBeamReconstructionFilter.h"
#if CUDA_FOUND
# include "rtkCudaFDKConeBeamReconstructionFilter.h"
#endif
#if OPENCL_FOUND
# include "rtkOpenCLFDKConeBeamReconstructionFilter.h"
#endif
#include "rtkFDKWarpBackProjectionImageFilter.h"
#include "rtkCyclicDeformationImageFilter.h"

#include <itkRegularExpressionSeriesFileNames.h>
#include <itkStreamingImageFilter.h>
#include <itkImageFileWriter.h>

int main(int argc, char * argv[])
{
  GGO(rtkfdk, args_info);

  typedef float OutputPixelType;
  const unsigned int Dimension = 3;

  typedef itk::Image< OutputPixelType, Dimension >     CPUOutputImageType;
#if CUDA_FOUND
  typedef itk::CudaImage< OutputPixelType, Dimension > OutputImageType;
#else
  typedef CPUOutputImageType                           OutputImageType;
#endif

  // Generate file names
  itk::RegularExpressionSeriesFileNames::Pointer names = itk::RegularExpressionSeriesFileNames::New();
  names->SetDirectory(args_info.path_arg);
  names->SetNumericSort(false);
  names->SetRegularExpression(args_info.regexp_arg);
  names->SetSubMatch(0);

  if(args_info.verbose_flag)
    std::cout << "Regular expression matches "
              << names->GetFileNames().size()
              << " file(s)..."
              << std::endl;

  // Projections reader
  typedef rtk::ProjectionsReader< OutputImageType > ReaderType;
  ReaderType::Pointer reader = ReaderType::New();
  reader->SetFileNames( names->GetFileNames() );
  TRY_AND_EXIT_ON_ITK_EXCEPTION( reader->GenerateOutputInformation() );

  itk::TimeProbe readerProbe;
  if(!args_info.lowmem_flag)
    {
    if(args_info.verbose_flag)
      std::cout << "Reading... " << std::flush;
    readerProbe.Start();
    TRY_AND_EXIT_ON_ITK_EXCEPTION( reader->Update() )
    readerProbe.Stop();
    if(args_info.verbose_flag)
      std::cout << "It took " << readerProbe.GetMean() << ' ' << readerProbe.GetUnit() << std::endl;
    }

  // Geometry
  if(args_info.verbose_flag)
    std::cout << "Reading geometry information from "
              << args_info.geometry_arg
              << "..."
              << std::endl;
  rtk::ThreeDCircularProjectionGeometryXMLFileReader::Pointer geometryReader;
  geometryReader = rtk::ThreeDCircularProjectionGeometryXMLFileReader::New();
  geometryReader->SetFilename(args_info.geometry_arg);
  TRY_AND_EXIT_ON_ITK_EXCEPTION( geometryReader->GenerateOutputInformation() )

  // Displaced detector weighting
  typedef rtk::DisplacedDetectorImageFilter< OutputImageType > DDFType;
  DDFType::Pointer ddf = DDFType::New();
  ddf->SetInput( reader->GetOutput() );
  ddf->SetGeometry( geometryReader->GetOutputObject() );

  // Short scan image filter
  typedef rtk::ParkerShortScanImageFilter< OutputImageType > PSSFType;
  PSSFType::Pointer pssf = PSSFType::New();
  pssf->SetInput( ddf->GetOutput() );
  pssf->SetGeometry( geometryReader->GetOutputObject() );
  pssf->InPlaceOff();

  // Create reconstructed image
  typedef rtk::ConstantImageSource< OutputImageType > ConstantImageSourceType;
  ConstantImageSourceType::Pointer constantImageSource = ConstantImageSourceType::New();
  rtk::SetConstantImageSourceFromGgo<ConstantImageSourceType, args_info_rtkfdk>(constantImageSource, args_info);

  // Motion-compensated objects for the compensation of a cyclic deformation.
  // Although these will only be used if the command line options for motion
  // compensation are set, we still create the object before hand to avoid auto
  // destruction.
  typedef itk::Vector<float,3> DVFPixelType;
  typedef itk::Image< DVFPixelType, 3 > DVFImageType;
  typedef rtk::CyclicDeformationImageFilter< DVFImageType > DeformationType;
  typedef itk::ImageFileReader<DeformationType::InputImageType> DVFReaderType;
  DVFReaderType::Pointer dvfReader = DVFReaderType::New();
  DeformationType::Pointer def = DeformationType::New();
  def->SetInput(dvfReader->GetOutput());
  typedef rtk::FDKWarpBackProjectionImageFilter<OutputImageType, OutputImageType, DeformationType> WarpBPType;
  WarpBPType::Pointer bp = WarpBPType::New();
  bp->SetDeformation(def);
  bp->SetGeometry( geometryReader->GetOutputObject() );

  // This macro sets options for fdk filter which I can not see how to do better
  // because TFFTPrecision is not the same, e.g. for CPU and CUDA (SR)
#define SET_FELDKAMP_OPTIONS(f) \
    f->SetInput( 0, constantImageSource->GetOutput() ); \
    f->SetInput( 1, pssf->GetOutput() ); \
    f->SetGeometry( geometryReader->GetOutputObject() ); \
    f->GetRampFilter()->SetTruncationCorrection(args_info.pad_arg); \
    f->GetRampFilter()->SetHannCutFrequency(args_info.hann_arg); \
    f->GetRampFilter()->SetHannCutFrequencyY(args_info.hannY_arg);

  // FDK reconstruction filtering
  typedef rtk::FDKConeBeamReconstructionFilter< OutputImageType > FDKCPUType;
  FDKCPUType::Pointer feldkamp = FDKCPUType::New();
#if OPENCL_FOUND
  typedef rtk::OpenCLFDKConeBeamReconstructionFilter FDKOPENCLType;
  FDKOPENCLType::Pointer feldkampOCL = FDKOPENCLType::New();
#endif
#if CUDA_FOUND
  typedef rtk::CudaFDKConeBeamReconstructionFilter FDKCUDAType;
  FDKCUDAType::Pointer feldkampCUDA = FDKCUDAType::New();
#endif
  itk::Image< OutputPixelType, Dimension > *pfeldkamp = NULL;
  if(!strcmp(args_info.hardware_arg, "cpu") )
    {
    typedef rtk::FDKConeBeamReconstructionFilter< OutputImageType > FDKCPUType;
    feldkamp = FDKCPUType::New();
    SET_FELDKAMP_OPTIONS( feldkamp );

    // Motion compensated CBCT settings
    if(args_info.signal_given && args_info.dvf_given)
      {
      dvfReader->SetFileName(args_info.dvf_arg);
      def->SetSignalFilename(args_info.signal_arg);
      feldkamp->SetBackProjectionFilter( bp.GetPointer() );
      }
    pfeldkamp = feldkamp->GetOutput();
    }
  else if(!strcmp(args_info.hardware_arg, "cuda") )
    {
#if CUDA_FOUND
    SET_FELDKAMP_OPTIONS( feldkampCUDA );
    pfeldkamp = feldkampCUDA->GetOutput();
#else
    std::cerr << "The program has not been compiled with cuda option" << std::endl;
    return EXIT_FAILURE;
#endif
    }
  else if(!strcmp(args_info.hardware_arg, "opencl") )
    {
#if OPENCL_FOUND
    SET_FELDKAMP_OPTIONS( feldkampOCL );
    pfeldkamp = feldkampOCL->GetOutput();
#else
    std::cerr << "The program has not been compiled with opencl option" << std::endl;
    return EXIT_FAILURE;
#endif
    }


  // Streaming depending on streaming capability of writer
  typedef itk::StreamingImageFilter<CPUOutputImageType, CPUOutputImageType> StreamerType;
  StreamerType::Pointer streamerBP = StreamerType::New();
  streamerBP->SetInput( pfeldkamp );
  streamerBP->SetNumberOfStreamDivisions( args_info.divisions_arg );

  // Write
  typedef itk::ImageFileWriter<CPUOutputImageType> WriterType;
  WriterType::Pointer writer = WriterType::New();
  writer->SetFileName( args_info.output_arg );
  writer->SetInput( streamerBP->GetOutput() );

  if(args_info.verbose_flag)
    std::cout << "Reconstructing and writing... " << std::flush;
  itk::TimeProbe writerProbe;

  writerProbe.Start();
  TRY_AND_EXIT_ON_ITK_EXCEPTION( writer->Update() );
  writerProbe.Stop();

  if(args_info.verbose_flag)
    {
    std::cout << "It took " << writerProbe.GetMean() << ' ' << readerProbe.GetUnit() << std::endl;
    if(!strcmp(args_info.hardware_arg, "cpu") )
      feldkamp->PrintTiming(std::cout);
#if CUDA_FOUND
    else if(!strcmp(args_info.hardware_arg, "cuda") )
      feldkampCUDA->PrintTiming(std::cout);
#endif
#if OPENCL_FOUND
    else if(!strcmp(args_info.hardware_arg, "opencl") )
      feldkampOCL->PrintTiming(std::cout);
#endif
    std::cout << std::endl;
    }

  return EXIT_SUCCESS;
}
