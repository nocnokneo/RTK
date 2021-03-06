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

#ifndef __rtkJosephBackProjectionImageFilter_txx
#define __rtkJosephBackProjectionImageFilter_txx

#include "rtkHomogeneousMatrix.h"
#include "rtkRayBoxIntersectionFunction.h"
#include "rtkThreeDCircularProjectionGeometry.h"

#include <itkImageRegionConstIterator.h>
#include <itkImageRegionIteratorWithIndex.h>
#include <itkIdentityTransform.h>

namespace rtk
{

template <class TInputImage, class TOutputImage>
void
JosephBackProjectionImageFilter<TInputImage,TOutputImage>
::GenerateData()
{
  const OutputImageRegionType outputRegionForThread = this->GetInput(1)->GetLargestPossibleRegion();
  if( outputRegionForThread != this->GetInput(1)->GetBufferedRegion() )
    {
    itkGenericExceptionMacro(<< "Largest and buffered region must be similar");
    }
  if( this->GetInPlace() )
    {
    itkGenericExceptionMacro(<< "Error, cannot be in place yet");
    }

  this->AllocateOutputs();
  this->GetOutput()->FillBuffer(0.);

  typename TOutputImage::Pointer weights = TOutputImage::New();
  weights->SetRegions( this->GetInput()->GetLargestPossibleRegion() );
  weights->Allocate();
  weights->FillBuffer(0);
  typename TInputImage::PixelType *beginBufferWeights = weights->GetBufferPointer();

  const unsigned int Dimension = TInputImage::ImageDimension;
  const unsigned int nPixelPerProj = outputRegionForThread.GetSize(0)*outputRegionForThread.GetSize(1);
  typename TOutputImage::PixelType *beginBuffer = this->GetOutput()->GetBufferPointer();
  unsigned int offsets[3];
  offsets[0] = 1;
  offsets[1] = this->GetInput(1)->GetBufferedRegion().GetSize()[0];
  offsets[2] = this->GetInput(1)->GetBufferedRegion().GetSize()[0] * this->GetInput(1)->GetBufferedRegion().GetSize()[1];
  GeometryType *geometry = dynamic_cast<GeometryType *>(this->GetGeometry().GetPointer());

  // Iterators on volume input and output
  typedef itk::ImageRegionConstIterator<TInputImage> InputRegionIterator;
  InputRegionIterator itIn(this->GetInput(1), outputRegionForThread);

  // Create intersection function
  typedef rtk::RayBoxIntersectionFunction<CoordRepType, Dimension> RBIFunctionType;
  typename RBIFunctionType::Pointer rbi[Dimension];
  for(unsigned int j=0; j<Dimension; j++)
    {
    rbi[j] = RBIFunctionType::New();
    typename RBIFunctionType::VectorType boxMin, boxMax;
    for(unsigned int i=0; i<Dimension; i++)
      {
      boxMin[i] = this->GetInput(0)->GetBufferedRegion().GetIndex()[i];
      boxMax[i] = boxMin[i] + this->GetInput(0)->GetBufferedRegion().GetSize()[i]-1;
      if(i==j)
        {
        boxMin[i] -= 0.5;
        boxMax[i] += 0.5;
        }
      }
    rbi[j]->SetBoxMin(boxMin);
    rbi[j]->SetBoxMax(boxMax);
    }

  // Go over each projection
  for(unsigned int iProj=outputRegionForThread.GetIndex(2);
                   iProj<outputRegionForThread.GetIndex(2)+outputRegionForThread.GetSize(2);
                   iProj++)
    {
    // Account for system rotations
    typename GeometryType::ThreeDHomogeneousMatrixType volPPToIndex;
    volPPToIndex = GetPhysicalPointToIndexMatrix( this->GetInput(0) );

    // Set source position in volume indices
    typename GeometryType::HomogeneousVectorType sourcePosition;
    sourcePosition = volPPToIndex * geometry->GetSourcePosition(iProj);
    for(unsigned int i=0; i<Dimension; i++)
      rbi[i]->SetRayOrigin( &(sourcePosition[0]) );

    // Compute matrix to transform projection index to volume index
    typename GeometryType::ThreeDHomogeneousMatrixType matrix;
    matrix = volPPToIndex.GetVnlMatrix() *
             geometry->GetProjectionCoordinatesToFixedSystemMatrix(iProj).GetVnlMatrix() *
             GetIndexToPhysicalPointMatrix( this->GetInput(1) ).GetVnlMatrix();

    // Go over each pixel of the projection
    typename RBIFunctionType::VectorType dirVox, stepMM, dirVoxAbs, np, fp;
    for(unsigned int pix=0; pix<nPixelPerProj; pix++, ++itIn)
      {
      // Compute point coordinate in volume depending on projection index
      for(unsigned int i=0; i<Dimension; i++)
        {
        dirVox[i] = matrix[i][Dimension];
        for(unsigned int j=0; j<Dimension; j++)
          dirVox[i] += matrix[i][j] * itIn.GetIndex()[j];

        // Direction
        dirVox[i] -= sourcePosition[i];
        }

      // Select main direction
      unsigned int mainDir = 0;
      for(unsigned int i=0; i<Dimension; i++)
        {
        dirVoxAbs[i] = vnl_math_abs( dirVox[i] );
        if(dirVoxAbs[i]>dirVoxAbs[mainDir])
          mainDir = i;
        }

      // Test if there is an intersection
      if( rbi[mainDir]->Evaluate(dirVox) )
        {
        dirVox.Normalize();

        // Compute and sort intersections: (n)earest and (f)arthest (p)points
        np = rbi[mainDir]->GetNearestPoint();
        fp = rbi[mainDir]->GetFarthestPoint();
        if(np[mainDir]>fp[mainDir])
          std::swap(np, fp);

        // Compute main nearest and farthest slice indices
        const int ns = vnl_math_ceil ( np[mainDir] );
        const int fs = vnl_math_floor( fp[mainDir] );

        // If its a corner, we can skip
        if( fs<ns )
          continue;

        // Determine the other two directions
        unsigned int notMainDirInf = (mainDir+1)%Dimension;
        unsigned int notMainDirSup = (mainDir+2)%Dimension;
        if(notMainDirInf>notMainDirSup)
          std::swap(notMainDirInf, notMainDirSup);

        // Init data pointers to first pixel of slice ns (i)nferior and (s)uperior (x|y) corner
        const unsigned int offsetx = offsets[notMainDirInf];
        const unsigned int offsety = offsets[notMainDirSup];
        const unsigned int offsetz = offsets[mainDir];
        OutputPixelType *pxiyi, *pxsyi, *pxiys, *pxsys;
        pxiyi = beginBuffer + ns * offsetz;
        pxsyi = pxiyi + offsetx;
        pxiys = pxiyi + offsety;
        pxsys = pxsyi + offsety;
        OutputPixelType *pxiyiw, *pxsyiw, *pxiysw, *pxsysw;
        pxiyiw = beginBufferWeights + ns * offsetz;
        pxsyiw = pxiyiw + offsetx;
        pxiysw = pxiyiw + offsety;
        pxsysw = pxsyiw + offsety;

        // Compute step size and go to first voxel
        const CoordRepType residual = ns-np[mainDir];
        const CoordRepType norm = 1/dirVox[mainDir];
        const CoordRepType stepx = dirVox[notMainDirInf] * norm;
        const CoordRepType stepy = dirVox[notMainDirSup] * norm;
        CoordRepType currentx = np[notMainDirInf] + residual*stepx;
        CoordRepType currenty = np[notMainDirSup] + residual*stepy;


        // For voxel to millimeters conversion
        stepMM[notMainDirInf] = this->GetInput(0)->GetSpacing()[notMainDirInf] * stepx;
        stepMM[notMainDirSup] = this->GetInput(0)->GetSpacing()[notMainDirSup] * stepy;
        stepMM[mainDir]       = this->GetInput(0)->GetSpacing()[mainDir];
        const CoordRepType stepLengthInMM = stepMM.GetNorm();

        // First step
        BilinearSplit(itIn.Get(), (residual+0.5) * stepLengthInMM,
                     pxiyi,  pxsyi,  pxiys,  pxsys,
                     pxiyiw, pxsyiw, pxiysw, pxsysw,
                     currentx, currenty, offsetx, offsety);

        // Middle steps
        for(int i=ns; i<fs; i++)
          {
          pxiyi += offsetz;
          pxsyi += offsetz;
          pxiys += offsetz;
          pxsys += offsetz;
          pxiyiw += offsetz;
          pxsyiw += offsetz;
          pxiysw += offsetz;
          pxsysw += offsetz;
          currentx += stepx;
          currenty += stepy;
          BilinearSplit(itIn.Get(), stepLengthInMM,
                       pxiyi,  pxsyi,  pxiys,  pxsys,
                       pxiyiw, pxsyiw, pxiysw, pxsysw,
                       currentx, currenty, offsetx, offsety);
          }

        // Last step
        BilinearSplit(itIn.Get(), (-0.5+fp[mainDir]-fs) * stepLengthInMM,
                      pxiyi,  pxsyi,  pxiys,  pxsys,
                      pxiyiw, pxsyiw, pxiysw, pxsysw,
                      currentx, currenty, offsetx, offsety);
        }
      }
    }

  // Final result
  typedef itk::ImageRegionIteratorWithIndex<TOutputImage> OutputRegionIterator;
  InputRegionIterator itInVol(this->GetInput(), this->GetInput()->GetLargestPossibleRegion());
  OutputRegionIterator itOut(this->GetOutput(), this->GetInput()->GetLargestPossibleRegion());
  OutputRegionIterator itW(weights, this->GetInput()->GetLargestPossibleRegion());
  while(!itOut.IsAtEnd())
    {
    if(itW.Get() != 0.)
      {
      itOut.Set(itInVol.Get() + itOut.Get() / itW.Get() );
      }
    else
      itOut.Set( itInVol.Get() );

    ++itInVol;
    ++itOut;
    ++itW;
    }
}

template <class TInputImage, class TOutputImage>
void
JosephBackProjectionImageFilter<TInputImage,TOutputImage>
::BilinearSplit(const InputPixelType ip,
                const CoordRepType stepLengthInMM,
                OutputPixelType *pxiyi,
                OutputPixelType *pxsyi,
                OutputPixelType *pxiys,
                OutputPixelType *pxsys,
                OutputPixelType *pxiyiw,
                OutputPixelType *pxsyiw,
                OutputPixelType *pxiysw,
                OutputPixelType *pxsysw,
                const CoordRepType x,
                const CoordRepType y,
                const unsigned int ox,
                const unsigned int oy)
{
  const unsigned int ix = vnl_math_floor(x);
  const unsigned int iy = vnl_math_floor(y);
  const unsigned int idx = ix*ox + iy*oy;
  const CoordRepType lx = x - ix;
  const CoordRepType ly = y - iy;
  const CoordRepType lxc = 1.-lx;
  const CoordRepType lyc = 1.-ly;

  const CoordRepType wii = lxc * lyc * stepLengthInMM;
  pxiyiw[idx] += wii;
  pxiyi[idx] += wii * ip;

  const CoordRepType wsi = lx * lyc * stepLengthInMM;
  pxsyiw[idx] += wsi;
  pxsyi[idx] += wsi * ip;

  const CoordRepType wis = lxc * ly * stepLengthInMM;
  pxiysw[idx] += wis;
  pxiys[idx] += wis * ip;

  const CoordRepType wss = lx * ly * stepLengthInMM;
  pxsysw[idx] += wss;
  pxsys[idx] += wss * ip;
}

} // end namespace rtk

#endif
