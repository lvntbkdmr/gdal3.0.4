/******************************************************************************
 *
 * Project:  LUM Reader
 * Purpose:  All code for LUM Image Format
 * Author:   Levent BEKDEMIR, lvntbkdmr@gmail.com
 *
 ******************************************************************************
 * Copyright (c) 2020, Levent BEKDEMIR <lvntbkdmr@gmail.com>
 *
 ****************************************************************************/

#include "cpl_port.h"
#include "gdal_frmts.h"
#include "rawdataset.h"

#include <algorithm>

//CPL_CVSID("$Id: lumdataset.cpp 8e5eeb35bf76390e3134a4ea7076dab7d478ea0e 2018-11-14 22:55:13 +0100 Levent BEKDEMIR $")

/************************************************************************/
/*                            swapByteOrder()                            */
/************************************************************************/
static void swapByteOrder(uint32_t &ui)
{
    ui = (ui >> 24) |
         ((ui << 8) & 0x00FF0000) |
         ((ui >> 8) & 0x0000FF00) |
         (ui << 24);
}

/************************************************************************/
/* ==================================================================== */
/*                              LUMDataset                              */
/* ==================================================================== */
/************************************************************************/

class LUMDataset final : public RawDataset
{
    VSILFILE *fpImage; // Image data file.

    bool bGeoTransformValid;
    double adfGeoTransform[6];

    CPL_DISALLOW_COPY_ASSIGN(LUMDataset)

public:
    LUMDataset();
    ~LUMDataset() override;

    CPLErr GetGeoTransform(double *) override;

    static int Identify(GDALOpenInfo *);
    static GDALDataset *Open(GDALOpenInfo *);
    static GDALDataset *Create(const char *pszFilename,
                               int nXSize, int nYSize, int nBands,
                               GDALDataType eType, char **papszOptions);
};

/************************************************************************/
/* ==================================================================== */
/*                              LUMDataset                             */
/* ==================================================================== */
/************************************************************************/

/************************************************************************/
/*                            LUMDataset()                             */
/************************************************************************/

LUMDataset::LUMDataset() : fpImage(nullptr),
                           bGeoTransformValid(false)
{
    adfGeoTransform[0] = 0.0;
    adfGeoTransform[1] = 1.0;
    adfGeoTransform[2] = 0.0;
    adfGeoTransform[3] = 0.0;
    adfGeoTransform[4] = 0.0;
    adfGeoTransform[5] = 1.0;
}

/************************************************************************/
/*                           ~LUMDataset()                             */
/************************************************************************/

LUMDataset::~LUMDataset()

{
    FlushCache();
    if (fpImage != nullptr && VSIFCloseL(fpImage) != 0)
    {
        CPLError(CE_Failure, CPLE_FileIO, "I/O error");
    }
}

/************************************************************************/
/*                          GetGeoTransform()                           */
/************************************************************************/

CPLErr LUMDataset::GetGeoTransform(double *padfTransform)
{
    if (bGeoTransformValid)
    {
        memcpy(padfTransform, adfGeoTransform, sizeof(double) * 6);
        return CE_None;
    }

    return CE_Failure;
}

/************************************************************************/
/*                              Identify()                              */
/************************************************************************/

int LUMDataset::Identify(GDALOpenInfo *poOpenInfo)

{
    if (poOpenInfo->nHeaderBytes < 12 || poOpenInfo->fpL == nullptr)
        return FALSE;

    const char *psHeader = reinterpret_cast<char *>(poOpenInfo->pabyHeader);
    if ((!STARTS_WITH_CI(psHeader + 8, "08BI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "09BI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "10BI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "11BI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "12BI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "13BI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "14BI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "15BI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "16BI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "08LI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "09LI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "10LI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "11LI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "12LI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "13LI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "14LI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "15LI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "16LI")) &&
        (!STARTS_WITH_CI(psHeader + 8, "FLOL")))
    {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *LUMDataset::Open(GDALOpenInfo *poOpenInfo)
{
    // Confirm that the header is compatible with a LUM dataset.
    if (!Identify(poOpenInfo))
        return nullptr;

    // Check that the file pointer from GDALOpenInfo* is available.
    if (poOpenInfo->fpL == nullptr)
    {
        return nullptr;
    }

    // Create a corresponding GDALDataset.
    LUMDataset *poDS = new LUMDataset();

    // Borrow the file pointer from GDALOpenInfo*.
    poDS->fpImage = poOpenInfo->fpL;
    poOpenInfo->fpL = nullptr;

    const char *psHeader = reinterpret_cast<char *>(poOpenInfo->pabyHeader);
    const uint32_t *pszSrc = reinterpret_cast<uint32_t *>(poOpenInfo->pabyHeader);

    uint32_t nWidth = static_cast<uint32_t>(pszSrc[0]);
    uint32_t nHeight = static_cast<uint32_t>(pszSrc[1]);

#ifdef CPL_LSB
    if (STARTS_WITH_CI(psHeader + 10, "BI"))
    {
        swapByteOrder(nWidth);
        swapByteOrder(nHeight);
    }
#else
    if (STARTS_WITH_CI(psHeader + 10, "LI"))
    {
        swapByteOrder(nWidth);
        swapByteOrder(nHeight);
    }
#endif

    poDS->nRasterXSize = nWidth;
    poDS->nRasterYSize = nHeight;
    if (poDS->nRasterXSize <= 0 || poDS->nRasterYSize <= 0)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Invalid dimensions : %d x %d",
                 poDS->nRasterXSize, poDS->nRasterYSize);
        delete poDS;
        return nullptr;
    }
    poDS->eAccess = poOpenInfo->eAccess;

    /* -------------------------------------------------------------------- */
    /*      Create band information objects.                                */
    /* -------------------------------------------------------------------- */
    bool bMSBFirst = false;

#ifdef CPL_LSB
    if (STARTS_WITH_CI(psHeader + 10, "BI"))
    {
        bMSBFirst = false;
    }
    else
    {
        bMSBFirst = true;
    }
#else
    if (STARTS_WITH_CI(psHeader + 10, "LI"))
    {
        bMSBFirst = false;
    }
    else
    {
        bMSBFirst = true;
    }
#endif

    GDALDataType eDataType = GDT_Unknown;
    if (STARTS_WITH_CI(psHeader + 8, "08"))
    {
        eDataType = GDT_Byte;
    }
    else
    {
        eDataType = GDT_UInt16;
    }

    const int iPixelSize = GDALGetDataTypeSizeBytes(eDataType);

    if (nWidth > INT_MAX / iPixelSize)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Int overflow occurred.");
        delete poDS;
        return nullptr;
    }
    poDS->SetBand(
        1, new RawRasterBand(poDS,
                             1,
                             poDS->fpImage,
                             nWidth * iPixelSize,
                             iPixelSize,
                             nWidth * iPixelSize,
                             eDataType,
                             bMSBFirst,
                             RawRasterBand::OwnFP::NO));
    poDS->GetRasterBand(1)->SetColorInterpretation(GCI_GrayIndex);

    /* -------------------------------------------------------------------- */
    /*      Check for world file.                                           */
    /* -------------------------------------------------------------------- */
    poDS->bGeoTransformValid = true;
    // CPL_TO_BOOL(
    //     GDALReadWorldFile( poOpenInfo->pszFilename, ".wld",
    //                        poDS->adfGeoTransform ) );
    // const double dfLLLat = 40.90179895060106;
    // const double dfLLLong = 109.7409133823373;
    // const double dfURLat = 40.76823286815466;
    // const double dfURLong = 109.6707990915811;

    // poDS->adfGeoTransform[0] = dfLLLong;
    // poDS->adfGeoTransform[3] = dfURLat;
    // poDS->adfGeoTransform[1] = (dfURLong - dfLLLong) / poDS->nRasterXSize;
    // poDS->adfGeoTransform[2] = 0.0;

    // poDS->adfGeoTransform[4] = 0.0;
    // poDS->adfGeoTransform[5] = -1 * (dfURLat - dfLLLat) / poDS->nRasterYSize;

    // Initialize any PAM information.
    poDS->SetDescription(poOpenInfo->pszFilename);
    poDS->TryLoadXML();

    // Check for overviews.
    poDS->oOvManager.Initialize(poDS, poOpenInfo->pszFilename);

    return poDS;
}

/************************************************************************/
/*                               Create()                               */
/************************************************************************/

GDALDataset *LUMDataset::Create(const char *pszFilename,
                                int nXSize, int nYSize, int nBands,
                                GDALDataType eType,
                                char **papszOptions)

{
    /* -------------------------------------------------------------------- */
    /*      Verify input options.                                           */
    /* -------------------------------------------------------------------- */
    if (eType != GDT_Byte && eType != GDT_UInt16)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Attempt to create LUM dataset with an illegal "
                 "data type (%s), only Byte and UInt16 supported.",
                 GDALGetDataTypeName(eType));

        return nullptr;
    }

    if (nBands != 1)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Attempt to create PNM dataset with an illegal number"
                 "of bands (%d).  Must be 1 (greyscale).",
                 nBands);

        return nullptr;
    }
    const CPLString osExt(CPLGetExtension(pszFilename));

    if (!EQUAL(osExt, "LUM"))
    {
        CPLError(CE_Warning, CPLE_AppDefined, "Extension for lum file should be .lum");
    }

    /* -------------------------------------------------------------------- */
    /*      Try to create the file.                                         */
    /* -------------------------------------------------------------------- */
    VSILFILE *fp = VSIFOpenL(pszFilename, "wb");
    if (fp == nullptr)
    {
        CPLError(CE_Failure, CPLE_OpenFailed,
                 "Attempt to create file `%s' failed.",
                 pszFilename);
        return nullptr;
    }

    /* -------------------------------------------------------------------- */
    /*      Write out the header.                                           */
    /* -------------------------------------------------------------------- */
    GUInt32 nImageWidth = static_cast<GUInt32>(nXSize);
    GUInt32 nImageHeight = static_cast<GUInt32>(nYSize);

    VSIFWriteL(&nImageWidth, 4, 1, fp);
    VSIFWriteL(&nImageHeight, 4, 1, fp);

    char szHeader[5] = {'\0'};

#ifdef CPL_LSB
    if (eType == GDT_Byte)
    {
        snprintf(szHeader, sizeof(szHeader), "%02dLI", 8);
    }
    else
    {
        snprintf(szHeader, sizeof(szHeader), "%02dLI", 12);
    }
#else
    if (eType == GDT_Byte)
    {
        snprintf(szHeader, sizeof(szHeader), "%02dBI", 8);
    }
    else
    {
        snprintf(szHeader, sizeof(szHeader), "%02dBI", 12);
    }
#endif

    bool bOK = VSIFWriteL(szHeader, strlen(szHeader), 1, fp) == 1;

    if (VSIFCloseL(fp) != 0)
        bOK = false;

    if (!bOK)
        return nullptr;

    GDALOpenInfo oOpenInfo(pszFilename, GA_Update);
    return Open(&oOpenInfo);
}

/************************************************************************/
/*                          GDALRegister_LUM()                          */
/************************************************************************/

void GDALRegister_LUM()
{
    if (GDALGetDriverByName("LUM") != nullptr)
        return;

    GDALDriver *poDriver = new GDALDriver();

    poDriver->SetDescription("LUM");
    poDriver->SetMetadataItem(GDAL_DCAP_RASTER, "YES");
    poDriver->SetMetadataItem(GDAL_DMD_LONGNAME, "LUM (.lum)");
    poDriver->SetMetadataItem(GDAL_DMD_HELPTOPIC, "frmt_various.html#LUM");
    poDriver->SetMetadataItem(GDAL_DMD_EXTENSION, "lum");
    poDriver->SetMetadataItem(GDAL_DMD_CREATIONDATATYPES, "Byte UInt16");
    poDriver->SetMetadataItem(GDAL_DCAP_VIRTUALIO, "YES");

    poDriver->pfnOpen = LUMDataset::Open;
    poDriver->pfnCreate = LUMDataset::Create;
    poDriver->pfnIdentify = LUMDataset::Identify;

    GetGDALDriverManager()->RegisterDriver(poDriver);
}
