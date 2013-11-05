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

#include "rtkinlinefdk_ggo.h"
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

#include <itkRegularExpressionSeriesFileNames.h>
#include <itkImageFileWriter.h>
#include <itkSimpleFastMutexLock.h>
#include <itkMultiThreader.h>
#include <itksys/SystemTools.hxx>
#include <itkNumericTraits.h>

// Pass projection name, projection parameters, last
struct ThreadInfoStruct
  {
  itk::SimpleFastMutexLock mutex;
  args_info_rtkinlinefdk *args_info;
  bool stop;
  unsigned int nproj;
  double sid;
  double sdd;
  double gantryAngle;
  double projOffsetX;
  double projOffsetY;
  double outOfPlaneAngle;
  double inPlaneAngle;
  double sourceOffsetX;
  double sourceOffsetY;
  double minimumOffsetX;    // Used for Wang weighting
  double maximumOffsetX;
  std::string fileName;
  };

void computeOffsetsFromGeometry(rtk::ThreeDCircularProjectionGeometry::Pointer geometry, double *minOffset,
                                double *maxOffset);

static ITK_THREAD_RETURN_TYPE AcquisitionCallback(void *arg);
static ITK_THREAD_RETURN_TYPE InlineThreadCallback(void *arg);

int main(int argc, char * argv[])
{
  GGO(rtkinlinefdk, args_info);

  // Launch threads, one for acquisition, one for reconstruction with inline
  // processing
  ThreadInfoStruct threadInfo;
  threadInfo.args_info = &args_info;
  threadInfo.nproj = 0;
  threadInfo.minimumOffsetX = 0.0;
  threadInfo.maximumOffsetX = 0.0;

  itk::MultiThreader::Pointer threader = itk::MultiThreader::New();
  threader->SetMultipleMethod(0, AcquisitionCallback, (void*)&threadInfo);
  threader->SetMultipleMethod(1, InlineThreadCallback, (void*)&threadInfo);
  threader->SetNumberOfThreads(2);
  TRY_AND_EXIT_ON_ITK_EXCEPTION( threader->MultipleMethodExecute () );

  return EXIT_SUCCESS;
}

// This thread reads in a geometry file and a sequence of projection file names
// and communicates them one by one to the other thread via a ThreadinfoStruct.
static ITK_THREAD_RETURN_TYPE AcquisitionCallback(void *arg)
{
  double minOffset, maxOffset;
  ThreadInfoStruct *threadInfo = (ThreadInfoStruct *)(((itk::MultiThreader::ThreadInfoStruct *)(arg))->UserData);

  threadInfo->mutex.Lock();

  // Generate file names
  itk::RegularExpressionSeriesFileNames::Pointer names = itk::RegularExpressionSeriesFileNames::New();
  names->SetDirectory(threadInfo->args_info->path_arg);
  names->SetNumericSort(false);
  names->SetRegularExpression(threadInfo->args_info->regexp_arg);
  names->SetSubMatch(0);

  if(threadInfo->args_info->verbose_flag)
    std::cout << "Regular expression matches "
              << names->GetFileNames().size()
              << " file(s)..."
              << std::endl;

  // Geometry
  if(threadInfo->args_info->verbose_flag)
    std::cout << "Reading geometry information from "
              << threadInfo->args_info->geometry_arg
              << "..."
              << std::endl;
  rtk::ThreeDCircularProjectionGeometryXMLFileReader::Pointer geometryReader;
  geometryReader = rtk::ThreeDCircularProjectionGeometryXMLFileReader::New();
  geometryReader->SetFilename(threadInfo->args_info->geometry_arg);
  TRY_AND_EXIT_ON_ITK_EXCEPTION( geometryReader->GenerateOutputInformation() );

  // Computes the minimum and maximum offsets from Geometry
  computeOffsetsFromGeometry(geometryReader->GetOutputObject(), &minOffset, &maxOffset);
  std::cout << " main :"<<  minOffset << " "<< maxOffset <<std::endl;

  threadInfo->mutex.Unlock();

  // Mock an inline acquisition
  unsigned int nproj = geometryReader->GetOutputObject()->GetMatrices().size();
  rtk::ThreeDCircularProjectionGeometry *geometry = geometryReader->GetOutputObject();
  for(unsigned int i=0; i<nproj; i++)
    {
    threadInfo->mutex.Lock();
    threadInfo->sdd = geometry->GetSourceToDetectorDistances()[i];
    threadInfo->sid = geometry->GetSourceToIsocenterDistances()[i];
    threadInfo->gantryAngle = geometry->GetGantryAngles()[i];
    threadInfo->sourceOffsetX = geometry->GetSourceOffsetsX()[i];
    threadInfo->sourceOffsetY = geometry->GetSourceOffsetsY()[i];
    threadInfo->projOffsetX = geometry->GetProjectionOffsetsX()[i];
    threadInfo->projOffsetY = geometry->GetProjectionOffsetsY()[i];
    threadInfo->inPlaneAngle = geometry->GetInPlaneAngles()[i];
    threadInfo->outOfPlaneAngle = geometry->GetOutOfPlaneAngles()[i];
    threadInfo->minimumOffsetX = minOffset;
    threadInfo->maximumOffsetX = maxOffset;
    threadInfo->fileName = names->GetFileNames()[ vnl_math_min( i, (unsigned int)names->GetFileNames().size()-1 ) ];
    threadInfo->nproj = i+1;
    threadInfo->stop = (i==nproj-1);
    if(threadInfo->args_info->verbose_flag)
      std::cout << std::endl
                << "AcquisitionCallback has simulated the acquisition of projection #" << i
                << std::endl;
    threadInfo->mutex.Unlock();
    itksys::SystemTools::Delay(200);
    }

  return ITK_THREAD_RETURN_VALUE;
}

// This thread receives information of each projection (one-by-one) and process
// directly the projections for which it has enough information. This thread
// currently assumes that the projections are sequentially sent with increasing
// gantry angles. Specific management with a queue must be implemented if the
// projections are not exactly sequential. Short scans has not been implemented
// yet because this filter currently require the full geometry of the
// acquisition. Management with a mock geometry file would be possible but it is
// still to be implemented.
static ITK_THREAD_RETURN_TYPE InlineThreadCallback(void *arg)
{
  ThreadInfoStruct *threadInfo = (ThreadInfoStruct *)(((itk::MultiThreader::ThreadInfoStruct *)(arg))->UserData);
  threadInfo->mutex.Lock();
  typedef float OutputPixelType;
  const unsigned int Dimension = 3;
  typedef itk::Image< OutputPixelType, Dimension >     CPUOutputImageType;
#if CUDA_FOUND
  typedef itk::CudaImage< OutputPixelType, Dimension > OutputImageType;
#else
  typedef CPUOutputImageType                           OutputImageType;
#endif

  rtk::ThreeDCircularProjectionGeometry::Pointer geometry = rtk::ThreeDCircularProjectionGeometry::New();
  std::vector< std::string >                     fileNames;

  // Projections reader
  typedef rtk::ProjectionsReader< OutputImageType > ReaderType;
  ReaderType::Pointer reader = ReaderType::New();

  // Create reconstructed image
  typedef rtk::ConstantImageSource< OutputImageType > ConstantImageSourceType;
  ConstantImageSourceType::Pointer constantImageSource = ConstantImageSourceType::New();
  rtk::SetConstantImageSourceFromGgo<ConstantImageSourceType, args_info_rtkinlinefdk>(constantImageSource, *(threadInfo->args_info));

  // Extract filter to process one projection at a time
  typedef itk::ExtractImageFilter<OutputImageType, OutputImageType> ExtractFilterType;
  ExtractFilterType::Pointer extract = ExtractFilterType::New();
  extract->SetInput( reader->GetOutput() );
  ExtractFilterType::InputImageRegionType subsetRegion;

  // Displaced detector weighting
  typedef rtk::DisplacedDetectorImageFilter< OutputImageType > DDFType;
  DDFType::Pointer ddf = DDFType::New();
  ddf->SetInput( extract->GetOutput() );
  ddf->SetGeometry( geometry );

  // Short scan image filter
//  typedef rtk::ParkerShortScanImageFilter< OutputImageType > PSSFType;
//  PSSFType::Pointer pssf = PSSFType::New();
//  pssf->SetInput( ddf->GetOutput() );
//  pssf->SetGeometry( geometryReader->GetOutputObject() );
//  pssf->InPlaceOff();

  // This macro sets options for fdk filter which I can not see how to do better
  // because TFFTPrecision is not the same, e.g. for CPU and CUDA (SR)
#define SET_FELDKAMP_OPTIONS(f) \
  f->SetInput( 0, constantImageSource->GetOutput() ); \
  f->SetInput( 1, ddf->GetOutput() ); \
  f->SetGeometry( geometry ); \
  f->GetRampFilter()->SetTruncationCorrection(threadInfo->args_info->pad_arg); \
  f->GetRampFilter()->SetHannCutFrequency(threadInfo->args_info->hann_arg);

  // FDK reconstruction filtering
  typedef rtk::FDKConeBeamReconstructionFilter< OutputImageType > FDKCPUType;
  FDKCPUType::Pointer feldkampCPU = FDKCPUType::New();
#if CUDA_FOUND
  typedef rtk::CudaFDKConeBeamReconstructionFilter FDKCUDAType;
  FDKCUDAType::Pointer feldkampCUDA = FDKCUDAType::New();
#endif
#if OPENCL_FOUND
  typedef rtk::OpenCLFDKConeBeamReconstructionFilter FDKOPENCLType;
  FDKOPENCLType::Pointer feldkampOCL = FDKOPENCLType::New();
#endif
  if(!strcmp(threadInfo->args_info->hardware_arg, "cpu") )
    {
    SET_FELDKAMP_OPTIONS(feldkampCPU);
    }
  else if(!strcmp(threadInfo->args_info->hardware_arg, "cuda") )
    {
#if CUDA_FOUND
    SET_FELDKAMP_OPTIONS( feldkampCUDA );
#else
    std::cerr << "The program has not been compiled with cuda option" << std::endl;
    exit(EXIT_FAILURE);
#endif
    }
  else if(!strcmp(threadInfo->args_info->hardware_arg, "opencl") )
    {
#if OPENCL_FOUND
    SET_FELDKAMP_OPTIONS( feldkampOCL );
#else
    std::cerr << "The program has not been compiled with opencl option" << std::endl;
    exit(EXIT_FAILURE);
#endif
    }

  // Writer
  typedef itk::ImageFileWriter<  CPUOutputImageType > WriterType;
  WriterType::Pointer writer = WriterType::New();
  writer->SetFileName( threadInfo->args_info->output_arg );

  threadInfo->mutex.Unlock();

  // Inline loop
  std::cout << "Reconstruction thread has entered in the processing loop" << std::endl;
  for(;;)
    {
    threadInfo->mutex.Lock();

    if(geometry->GetMatrices().size()<threadInfo->nproj)
      {
      if(threadInfo->args_info->verbose_flag)
        std::cerr << "InlineThreadCallback has received projection #" << threadInfo->nproj-1 << std::endl;

      if(threadInfo->fileName != "" && (fileNames.size()==0 || fileNames.back() != threadInfo->fileName) )
        fileNames.push_back(threadInfo->fileName);

      geometry->AddProjection(threadInfo->sid, threadInfo->sdd, threadInfo->gantryAngle,
                              threadInfo->projOffsetX, threadInfo->projOffsetY,
                              threadInfo->outOfPlaneAngle, threadInfo->inPlaneAngle,
                              threadInfo->sourceOffsetX, threadInfo->sourceOffsetY);

      std::cout << "Geometry size : "<<geometry->GetMatrices().size() << std::endl;

      if(geometry->GetMatrices().size()!=threadInfo->nproj)
        {
        std::cerr << "Missed one projection in InlineThreadCallback" << std::endl;
        exit(EXIT_FAILURE);
        }
      if(geometry->GetMatrices().size()<3)
        {
        threadInfo->mutex.Unlock();
        continue;
        }

      OutputImageType::RegionType region = reader->GetOutput()->GetLargestPossibleRegion();
      std::cout<<"Reader size : " << region.GetSize()[0]<<" "
               << region.GetSize()[1]<<" "<< region.GetSize()[2]<<std::endl;
      std::cout << "Reader index : "<<region.GetIndex()[0]<< " "
                << region.GetIndex()[1]<< " "
                << region.GetIndex()[2]<<std::endl;

      reader->SetFileNames( fileNames );
      reader->UpdateOutputInformation();
      subsetRegion = reader->GetOutput()->GetLargestPossibleRegion();
      subsetRegion.SetIndex(Dimension-1, geometry->GetMatrices().size()-2);
      subsetRegion.SetSize(Dimension-1, 1);
      extract->SetExtractionRegion(subsetRegion);

      std::cout << "Region size : "<<subsetRegion.GetSize()[0]<< " "
                << subsetRegion.GetSize()[1]<< " "
                << subsetRegion.GetSize()[2]<<std::endl;
      std::cout << "Region index : "<<subsetRegion.GetIndex()[0]<< " "
                << subsetRegion.GetIndex()[1]<< " "
                << subsetRegion.GetIndex()[2]<<std::endl;

      ExtractFilterType::InputImageRegionType extractRegion = extract->GetOutput()->GetLargestPossibleRegion();

      std::cout << "Extract region size : "<<extractRegion.GetSize()[0]<< " "
                << extractRegion.GetSize()[1]<< " "
                << extractRegion.GetSize()[2]<<std::endl;
      std::cout << "Extract region index : "<<extractRegion.GetIndex()[0]<< " "
                << extractRegion.GetIndex()[1]<< " "
                << extractRegion.GetIndex()[2]<<std::endl;

      ddf->SetOffsets(threadInfo->minimumOffsetX, threadInfo->maximumOffsetX);

      if(!strcmp(threadInfo->args_info->hardware_arg, "cpu") )
        {
        TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCPU->Update() );
        OutputImageType::Pointer pimg = feldkampCPU->GetOutput();
        pimg->DisconnectPipeline();
        feldkampCPU->SetInput( pimg );
        TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCPU->GetOutput()->UpdateOutputInformation() );
        TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCPU->GetOutput()->PropagateRequestedRegion() );
        }
#if CUDA_FOUND
      else if(!strcmp(threadInfo->args_info->hardware_arg, "cuda") )
        {
        TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCUDA->Update() );
        OutputImageType::Pointer pimg = feldkampCUDA->GetOutput();
        pimg->DisconnectPipeline();
        feldkampCUDA->SetInput( pimg );
        TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCUDA->GetOutput()->UpdateOutputInformation() );
        TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCUDA->GetOutput()->PropagateRequestedRegion() );
        }
#endif
#if OPENCL_FOUND
      else if(!strcmp(threadInfo->args_info->hardware_arg, "opencl") )
        {
        TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampOCL->Update() );
        CPUOutputImageType::Pointer pimg = feldkampOCL->GetOutput();
        pimg->DisconnectPipeline();
        feldkampOCL->SetInput( pimg );
        TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampOCL->GetOutput()->UpdateOutputInformation() );
        TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampOCL->GetOutput()->PropagateRequestedRegion() );
        }
#endif
      if(threadInfo->args_info->verbose_flag)
        std::cout << "Projection #" << subsetRegion.GetIndex(Dimension-1)
                  << " has been processed in reconstruction." << std::endl;

      if(threadInfo->stop)
        {
        // Process first projection
        subsetRegion.SetIndex(Dimension-1, 0);
        extract->SetExtractionRegion(subsetRegion);
        if(!strcmp(threadInfo->args_info->hardware_arg, "cpu") )
          {
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCPU->Update() );
          OutputImageType::Pointer pimg = feldkampCPU->GetOutput();
          pimg->DisconnectPipeline();
          feldkampCPU->SetInput( pimg );
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCPU->GetOutput()->UpdateOutputInformation() );
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCPU->GetOutput()->PropagateRequestedRegion() );
          }
#if CUDA_FOUND
        else if(!strcmp(threadInfo->args_info->hardware_arg, "cuda") )
          {
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCUDA->Update() );
          OutputImageType::Pointer pimg = feldkampCUDA->GetOutput();
          pimg->DisconnectPipeline();
          feldkampCUDA->SetInput( pimg );
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCUDA->GetOutput()->UpdateOutputInformation() );
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCUDA->GetOutput()->PropagateRequestedRegion() );
          }
#endif
#if OPENCL_FOUND
        else if(!strcmp(threadInfo->args_info->hardware_arg, "opencl") )
          {
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampOCL->Update() );
          CPUOutputImageType::Pointer pimg = feldkampOCL->GetOutput();
          pimg->DisconnectPipeline();
          feldkampOCL->SetInput( pimg );
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampOCL->GetOutput()->UpdateOutputInformation() );
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampOCL->GetOutput()->PropagateRequestedRegion() );
          }
#endif
        if(threadInfo->args_info->verbose_flag)
          std::cout << "Projection #" << subsetRegion.GetIndex(Dimension-1)
                    << " has been processed in reconstruction." << std::endl;

        // Process last projection
        subsetRegion.SetIndex(Dimension-1, geometry->GetMatrices().size()-1);
        extract->SetExtractionRegion(subsetRegion);
        if(!strcmp(threadInfo->args_info->hardware_arg, "cpu") )
          {
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCPU->Update() );
          writer->SetInput( feldkampCPU->GetOutput() );
          }
#if CUDA_FOUND
        else if(!strcmp(threadInfo->args_info->hardware_arg, "cuda") )
          {
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampCUDA->Update() );
          writer->SetInput( feldkampCUDA->GetOutput() );
          }
#endif
#if OPENCL_FOUND
        else if(!strcmp(threadInfo->args_info->hardware_arg, "opencl") )
          {
          TRY_AND_EXIT_ON_ITK_EXCEPTION( feldkampOCL->Update() );
          writer->SetInput( feldkampOCL->GetOutput() );
          }
#endif
        if(threadInfo->args_info->verbose_flag)
              std::cout << "Projection #" << subsetRegion.GetIndex(Dimension-1)
                    << " has been processed in reconstruction." << std::endl;

        //Write to disk and exit
        TRY_AND_EXIT_ON_ITK_EXCEPTION( writer->Update() );
        exit(EXIT_SUCCESS);
        }
      }

    threadInfo->mutex.Unlock();
    }
  return ITK_THREAD_RETURN_VALUE;
}

void computeOffsetsFromGeometry(rtk::ThreeDCircularProjectionGeometry::Pointer geometry, double *minOffset,
                                double *maxOffset)
{
  double min = itk::NumericTraits<double>::max();
  double max = itk::NumericTraits<double>::min();

  for(unsigned int i=0; i<geometry->GetProjectionOffsetsX().size(); i++)
    {
    min = vnl_math_min(min, geometry->GetProjectionOffsetsX()[i]);
    max = vnl_math_max(max, geometry->GetProjectionOffsetsX()[i]);
    }
  *minOffset = min;
  *maxOffset = max;
}

