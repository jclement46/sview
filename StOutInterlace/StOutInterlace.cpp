/**
 * Copyright © 2009-2013 Kirill Gavrilov <kirill@sview.ru>
 *
 * StOutInterlace library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * StOutInterlace library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "StOutInterlace.h"

#include <StGL/StGLContext.h>
#include <StGL/StGLEnums.h>
#include <StGL/StGLVec.h>
#include <StGLCore/StGLCore20.h>
#include <StSettings/StSettings.h>
#include <StSettings/StTranslations.h>
#include <StSettings/StEnumParam.h>
#include <StCore/StSearchMonitors.h>
#include <StVersion.h>

namespace {

    static const char ST_OUT_PLUGIN_NAME[] = "StOutInterlace";

    // shaders data
    static const char VSHADER_ED[]              = "vED.shv";
    static const char FSHADER_EDINTERLACE_ON[]  = "fEDinterlace.shf";
    static const char FSHADER_ED_OFF[]          = "fEDoff.shf";

    static const char ST_SETTING_DEVICE_ID[]    = "deviceId";
    static const char ST_SETTING_WINDOWPOS[]    = "windowPos";
    static const char ST_SETTING_BIND_MONITOR[] = "bindMonitor";
    static const char ST_SETTING_VSYNC[]        = "vsync";
    static const char ST_SETTING_REVERSE[]      = "reverse";

    struct StMonInterlacedInfo_t {
        const stUtf8_t* pnpid;
        bool            isReversed;
    };

    /**
     * Database of known interlaced monitors.
     */
    static const StMonInterlacedInfo_t THE_KNOWN_MONITORS[] = {
        {"ZMT1900", false}, // Zalman Trimon M190S
        {"ZMT2200", false}, // Zalman Trimon M220W
        {"ENV2373", true }, // Envision
        {"HIT8002", false}, // Hyundai W220S D-Sub
        {"HIT8D02", false}, // Hyundai W220S DVID
        {"HIT7003", false}, // Hyundai W240S D-Sub
        {"HIT7D03", false}, // Hyundai W240S D-Sub
      //{"ACI23C2", false}, // ASUS VG23AH
        {"ACI27C2", false}, // ASUS VG27AH
        {       "", false}  // NULL-terminate array
    };

    // translation resources
    enum {
        STTR_HINTERLACE_NAME    = 1000,
        STTR_HINTERLACE_DESC    = 1001,
        STTR_VINTERLACE_NAME    = 1002,
        STTR_VINTERLACE_DESC    = 1003,
        STTR_CHESSBOARD_NAME    = 1006,
        STTR_CHESSBOARD_DESC    = 1007,
        STTR_HINTERLACE_ED_NAME = 1008,
        STTR_HINTERLACE_ED_DESC = 1009,

        // parameters
        STTR_PARAMETER_VSYNC    = 1100,
        STTR_PARAMETER_REVERSE  = 1102,
        STTR_PARAMETER_BIND_MON = 1103,

        // about info
        STTR_PLUGIN_TITLE       = 2000,
        STTR_VERSION_STRING     = 2001,
        STTR_PLUGIN_DESCRIPTION = 2002,
    };

    static const char* ST_SHADER_TEMPLATE[3] = {
        "uniform sampler2D uTexture;\n"
        "varying vec2 fTexCoord;\n"
        "void main(void) {\n",
        "\n",
        "    gl_FragColor = texture2D(uTexture, fTexCoord);\n"
        "}\n"
    };

    static const StGLVarLocation ST_VATTRIB_VERTEX(0);
    static const StGLVarLocation ST_VATTRIB_TCOORD(1);

};

StProgramFB::StProgramFB(const StString& theTitle)
: StGLProgram(theTitle) {
    //
}

bool StProgramFB::link(StGLContext& theCtx) {
    StGLProgram::bindAttribLocation(theCtx, "vVertex",   ST_VATTRIB_VERTEX);
    StGLProgram::bindAttribLocation(theCtx, "vTexCoord", ST_VATTRIB_TCOORD);

    if(!StGLProgram::link(theCtx)) {
        return false;
    }
    StGLVarLocation aTextureLoc = StGLProgram::getUniformLocation(theCtx, "uTexture");
    if(aTextureLoc.isValid()) {
        use(theCtx);
        theCtx.core20fwd->glUniform1i(aTextureLoc, StGLProgram::TEXTURE_SAMPLE_0); // GL_TEXTURE0
        unuse(theCtx);
    }
    return aTextureLoc.isValid();
}

StAtomic<int32_t> StOutInterlace::myInstancesNb(0);

inline bool isInterlacedMonitor(const StMonitor& theMon,
                                bool&            theIsReversed) {
    if(theMon.getPnPId().getSize() != 7) {
        return false;
    }
    for(size_t anIter = 0;; ++anIter) {
        const StMonInterlacedInfo_t& aMon = THE_KNOWN_MONITORS[anIter];
        if(aMon.pnpid[0] == '\0') {
            return false;
        } else if(stAreEqual(aMon.pnpid, theMon.getPnPId().toCString(), 7)) {
            theIsReversed = aMon.isReversed;
            return true;
        }
    }
}

StHandle<StMonitor> StOutInterlace::getHInterlaceMonitor(const StArrayList<StMonitor>& theMonitors,
                                                         bool& theIsReversed) {
    for(size_t aMonIter = 0; aMonIter < theMonitors.size(); ++aMonIter) {
        const StMonitor& aMon = theMonitors[aMonIter];
        if(isInterlacedMonitor(aMon, theIsReversed)) {
            return new StMonitor(aMon);
        }
    }
    return NULL;
}

StString StOutInterlace::getRendererAbout() const {
    return myAbout;
}

const char* StOutInterlace::getRendererId() const {
    return ST_OUT_PLUGIN_NAME;
}

const char* StOutInterlace::getDeviceId() const {
    switch(myDevice) {
        case DEVICE_VINTERLACE:    return "Col";
        case DEVICE_CHESSBOARD:    return "Chess";
        case DEVICE_HINTERLACE_ED: return "RowED";
        case DEVICE_HINTERLACE:
        default:                   return "Row";
    }
}

bool StOutInterlace::setDevice(const StString& theDevice) {
    if(theDevice == "Row") {
        myDevice = DEVICE_HINTERLACE;
    } else if(theDevice == "Col") {
        myDevice = DEVICE_VINTERLACE;
    } else if(theDevice == "Chess") {
        myDevice = DEVICE_CHESSBOARD;
    } else if(theDevice == "RowED") {
        myDevice = DEVICE_HINTERLACE_ED;
    }
    return false;
}

void StOutInterlace::getDevices(StOutDevicesList& theList) const {
    for(size_t anIter = 0; anIter < myDevices.size(); ++anIter) {
        theList.add(myDevices[anIter]);
    }
}

void StOutInterlace::getOptions(StParamsList& theList) const {
    theList.add(params.IsVSyncOn);
    theList.add(params.ToReverse);
    theList.add(params.BindToMon);
}

StOutInterlace::StOutInterlace(const StNativeWin_t theParentWindow)
: StWindow(theParentWindow),
  mySettings(new StSettings(ST_OUT_PLUGIN_NAME)),
  myFrmBuffer(new StGLFrameBuffer()),
  myDevice(DEVICE_AUTO),
  myEDTimer(true),
  myEDIntelaceOn(new StGLProgram("ED Interlace On")),
  myEDOff(new StGLProgram("ED Interlace Off")),
  myVpSizeY(10),
  myToSavePlacement(theParentWindow == (StNativeWin_t )NULL),
  myIsMonReversed(false),
  myIsStereo(false),
  myIsEDactive(false),
  myIsEDCodeFinished(false),
  myToCompressMem(myInstancesNb.increment() > 1),
  myIsBroken(false) {
    const StSearchMonitors& aMonitors = StWindow::getMonitors();
    StTranslations aLangMap(ST_OUT_PLUGIN_NAME);

    myGlPrograms[DEVICE_HINTERLACE] = new StProgramFB("Row Interlace");
    myGlPrograms[DEVICE_VINTERLACE] = new StProgramFB("Column Interlace");
    myGlPrograms[DEVICE_CHESSBOARD] = new StProgramFB("Chessboard");
    myGlPrograms[DEVICE_HINTERLACE_ED] = myGlPrograms[DEVICE_HINTERLACE];

    myGlProgramsRev[DEVICE_HINTERLACE] = new StProgramFB("Row Interlace Inversed");
    myGlProgramsRev[DEVICE_VINTERLACE] = new StProgramFB("Column Interlace Inversed");
    myGlProgramsRev[DEVICE_CHESSBOARD] = new StProgramFB("Chessboard Inversed");
    myGlProgramsRev[DEVICE_HINTERLACE_ED] = myGlProgramsRev[DEVICE_HINTERLACE];

    // about string
    StString& aTitle     = aLangMap.changeValueId(STTR_PLUGIN_TITLE,   "sView - Interlaced Output library");
    StString& aVerString = aLangMap.changeValueId(STTR_VERSION_STRING, "version");
    StString& aDescr     = aLangMap.changeValueId(STTR_PLUGIN_DESCRIPTION,
        "(C) 2009-2013 Kirill Gavrilov <kirill@sview.ru>\nOfficial site: www.sview.ru\n\nThis library distributed under LGPL3.0");
    myAbout = aTitle + '\n' + aVerString + ": " + StVersionInfo::getSDKVersionString() + "\n \n" + aDescr;

    // devices list
    StHandle<StOutDevice> aDevRow = new StOutDevice();
    aDevRow->PluginId = ST_OUT_PLUGIN_NAME;
    aDevRow->DeviceId = "Row";
    aDevRow->Priority = ST_DEVICE_SUPPORT_NONE;
    aDevRow->Name     = aLangMap.changeValueId(STTR_HINTERLACE_NAME, "Row Interlaced");
    aDevRow->Desc     = aLangMap.changeValueId(STTR_HINTERLACE_DESC, "Row interlaced displays: Zalman, Hyundai,...");
    myDevices.add(aDevRow);

    StHandle<StOutDevice> aDevCol = new StOutDevice();
    aDevCol->PluginId = ST_OUT_PLUGIN_NAME;
    aDevCol->DeviceId = "Col";
    aDevCol->Priority = ST_DEVICE_SUPPORT_NONE;
    aDevCol->Name     = aLangMap.changeValueId(STTR_VINTERLACE_NAME, "Column Interlaced");
    aDevCol->Desc     = aLangMap.changeValueId(STTR_VINTERLACE_DESC, "Column interlaced displays");
    myDevices.add(aDevCol);

    StHandle<StOutDevice> aDevChess = new StOutDevice();
    aDevChess->PluginId = ST_OUT_PLUGIN_NAME;
    aDevChess->DeviceId = "Chess";
    aDevChess->Priority = ST_DEVICE_SUPPORT_NONE;
    aDevChess->Name     = aLangMap.changeValueId(STTR_CHESSBOARD_NAME, "DLP TV (chessboard)");
    aDevChess->Desc     = aLangMap.changeValueId(STTR_CHESSBOARD_DESC, "DLP TV (chessboard)");
    myDevices.add(aDevChess);

    StHandle<StOutDevice> aDevED = new StOutDevice();
    aDevED->PluginId = ST_OUT_PLUGIN_NAME;
    aDevED->DeviceId = "RowED";
    aDevED->Priority = ST_DEVICE_SUPPORT_NONE;
    aDevED->Name     = aLangMap.changeValueId(STTR_HINTERLACE_ED_NAME, "Interlaced ED");
    aDevED->Desc     = aLangMap.changeValueId(STTR_HINTERLACE_ED_DESC, "EDimensional in interlaced mode");
    myDevices.add(aDevED);

    // detect connected displays
    bool myIsMonReversed = false;
    StHandle<StMonitor> aMon = StOutInterlace::getHInterlaceMonitor(aMonitors, myIsMonReversed);
    if(!aMon.isNull()) {
        aDevRow->Priority = ST_DEVICE_SUPPORT_HIGHT;
    }

    // options
    params.IsVSyncOn = new StBoolParamNamed(true,  aLangMap.changeValueId(STTR_PARAMETER_VSYNC,    "VSync"));
    params.ToReverse = new StBoolParamNamed(false, aLangMap.changeValueId(STTR_PARAMETER_REVERSE,  "Reverse Order"));
    params.BindToMon = new StBoolParamNamed(true,  aLangMap.changeValueId(STTR_PARAMETER_BIND_MON, "Bind To Supported Monitor"));
    mySettings->loadParam(ST_SETTING_VSYNC,        params.IsVSyncOn);
    mySettings->loadParam(ST_SETTING_REVERSE,      params.ToReverse);
    mySettings->loadParam(ST_SETTING_BIND_MONITOR, params.BindToMon);

    params.IsVSyncOn->signals.onChanged.connect(this, &StOutInterlace::doVSync);
    params.BindToMon->signals.onChanged.connect(this, &StOutInterlace::doSetBindToMonitor);

    // load window position
    StRect<int32_t> aRect(256, 768, 256, 1024);
    bool isLoadedPosition = mySettings->loadInt32Rect(ST_SETTING_WINDOWPOS, aRect);
    StMonitor aMonitor = aMonitors[aRect.center()];
    if(params.BindToMon->getValue()
    && !aMon.isNull()
    && !isInterlacedMonitor(aMonitor, myIsMonReversed)) {
        aMonitor = *aMon;
    }
    if(isLoadedPosition) {
        if(!aMonitor.getVRect().isPointIn(aRect.center())) {
            ST_DEBUG_LOG("Warning, stored window position is out of the monitor(" + aMonitor.getId() + ")!" + aRect.toString());
            const int aWidth  = aRect.width();
            const int aHeight = aRect.height();
            aRect.left()   = aMonitor.getVRect().left() + 256;
            aRect.right()  = aRect.left() + aWidth;
            aRect.top()    = aMonitor.getVRect().top() + 256;
            aRect.bottom() = aRect.top() + aHeight;
        }
    } else {
        // try to open window on correct display
        aRect = aMonitor.getVRect();
        aRect.left()   = aRect.left() + 256;
        aRect.right()  = aRect.left() + 1024;
        aRect.top()    = aRect.top()  + 256;
        aRect.bottom() = aRect.top()  + 512;
    }
    StWindow::setPlacement(aRect);

    // load device settings
    mySettings->loadInt32(ST_SETTING_DEVICE_ID, myDevice);
    if(myDevice == DEVICE_AUTO) {
        myDevice = DEVICE_HINTERLACE;
    }

    // request slave window
    StWinAttributes_t anAttribs = stDefaultWinAttributes();
    StWindow::getAttributes(anAttribs);
    anAttribs.isSlave = true;
    anAttribs.isSlaveHLineTop = true;
    anAttribs.isSlaveHide = true;
    StWindow::setAttributes(anAttribs);
}

void StOutInterlace::releaseResources() {
    if(!myContext.isNull()) {
        for(size_t anIter = 0; anIter < DEVICE_NB; ++anIter) {
            myGlPrograms   [anIter]->release(*myContext);
            myGlProgramsRev[anIter]->release(*myContext);
        }
        myEDIntelaceOn->release(*myContext);
        myEDOff->release(*myContext);
        myQuadVertBuf.release(*myContext);
        myQuadTexCoordBuf.release(*myContext);
        myFrmBuffer->release(*myContext);
    }
    myContext.nullify();

    // read windowed placement
    StWindow::hide();
    if(myToSavePlacement) {
        StWindow::setFullScreen(false);
        mySettings->saveInt32Rect(ST_SETTING_WINDOWPOS, StWindow::getPlacement());
    }
    mySettings->saveParam(ST_SETTING_BIND_MONITOR, params.BindToMon);
    mySettings->saveParam(ST_SETTING_VSYNC,        params.IsVSyncOn);
    mySettings->saveParam(ST_SETTING_REVERSE,      params.ToReverse);
    mySettings->saveInt32(ST_SETTING_DEVICE_ID,    myDevice);
}

StOutInterlace::~StOutInterlace() {
    myInstancesNb.decrement();
    releaseResources();
}

void StOutInterlace::close() {
    releaseResources();
    StWindow::close();
}

bool StOutInterlace::create() {
    StWindow::show();
    if(!StWindow::create()) {
        return false;
    }

    // initialize GL context
    myContext = new StGLContext();
    if(!myContext->stglInit()) {
        stError(StString(ST_OUT_PLUGIN_NAME) + " Plugin, OpenGL context is broken!\n(OpenGL library internal error?)");
        return false;
    } else if(!myContext->isGlGreaterEqual(2, 0)) {
        stError(StString(ST_OUT_PLUGIN_NAME) + " Plugin, OpenGL2.0+ not available!");
        return false;
    }
    myContext->stglSetVSync(params.IsVSyncOn->getValue() ? StGLContext::VSync_ON : StGLContext::VSync_OFF);

    // INIT shaders
    StString aShadersError(StString(ST_OUT_PLUGIN_NAME) + " Plugin, Failed to init Shaders");
    StGLVertexShader aVertexShader("Interlace"); // common vertex shader
    StGLAutoRelease aTmp1(*myContext, aVertexShader);
    if(!aVertexShader.init(*myContext,
                           "attribute vec4 vVertex;\n"
                           "attribute vec2 vTexCoord;\n"
                           "varying vec2 fTexCoord;\n"
                           "void main(void) {\n"
                           "  fTexCoord = vTexCoord;\n"
                           "  gl_Position = vVertex;\n"
                           "}\n")) {
        stError(aShadersError);
        return false;
    }

    // row interlaced
    StGLFragmentShader aShaderRow(myGlPrograms[DEVICE_HINTERLACE]->getTitle());
    StGLFragmentShader aShaderRowRev(myGlProgramsRev[DEVICE_HINTERLACE]->getTitle());
    StGLAutoRelease aTmp2(*myContext, aShaderRow);
    StGLAutoRelease aTmp3(*myContext, aShaderRowRev);
    if(!aShaderRow.init(*myContext,
                        ST_SHADER_TEMPLATE[0],
                        // drop odd horizontal line (starts from bottom)
                        "if(int(mod(gl_FragCoord.y + 1.5, 2.0)) == 1) { discard; }\n",
                        ST_SHADER_TEMPLATE[2])) {
        stError(aShadersError);
        return false;
    } else if(!aShaderRowRev.init(*myContext,
                                  ST_SHADER_TEMPLATE[0],
              // drop even horizontal line (starts from bottom)
              "if(int(mod(gl_FragCoord.y + 1.5, 2.0)) != 1) { discard; }\n",
              ST_SHADER_TEMPLATE[2])) {
        stError(aShadersError);
        return false;
    }
    myGlPrograms   [DEVICE_HINTERLACE]->create(*myContext)
                                       .attachShader(*myContext, aVertexShader)
                                       .attachShader(*myContext, aShaderRow)
                                       .link(*myContext);
    myGlProgramsRev[DEVICE_HINTERLACE]->create(*myContext)
                                       .attachShader(*myContext, aVertexShader)
                                       .attachShader(*myContext, aShaderRowRev)
                                       .link(*myContext);

    // column interlaced
    StGLFragmentShader aShaderCol(myGlPrograms[DEVICE_VINTERLACE]->getTitle());
    StGLFragmentShader aShaderColRev(myGlProgramsRev[DEVICE_VINTERLACE]->getTitle());
    StGLAutoRelease aTmp4(*myContext, aShaderCol);
    StGLAutoRelease aTmp5(*myContext, aShaderColRev);
    if(!aShaderCol.init(*myContext,
                        ST_SHADER_TEMPLATE[0],
                        // drop odd column (starts from left)
                        "if(int(mod(gl_FragCoord.x + 1.5, 2.0)) != 1) { discard; }\n",
                        ST_SHADER_TEMPLATE[2])) {
        stError(aShadersError);
        return false;
    } else if(!aShaderColRev.init(*myContext,
                                  ST_SHADER_TEMPLATE[0],
              // drop even column (starts from left)
              "if(int(mod(gl_FragCoord.x + 1.5, 2.0)) == 1) { discard; }\n",
              ST_SHADER_TEMPLATE[2])) {
        stError(aShadersError);
        return false;
    }
    myGlPrograms   [DEVICE_VINTERLACE]->create(*myContext)
                                       .attachShader(*myContext, aVertexShader)
                                       .attachShader(*myContext, aShaderCol)
                                       .link(*myContext);
    myGlProgramsRev[DEVICE_VINTERLACE]->create(*myContext)
                                       .attachShader(*myContext, aVertexShader)
                                       .attachShader(*myContext, aShaderColRev)
                                       .link(*myContext);

    // chessboard
    StGLFragmentShader aShaderChess(myGlPrograms[DEVICE_CHESSBOARD]->getTitle());
    StGLFragmentShader aShaderChessRev(myGlProgramsRev[DEVICE_CHESSBOARD]->getTitle());
    StGLAutoRelease aTmp6(*myContext, aShaderChess);
    StGLAutoRelease aTmp7(*myContext, aShaderChessRev);
    if(!aShaderChess.init(*myContext,
                          ST_SHADER_TEMPLATE[0],
                          "bool isEvenX = int(mod(floor(gl_FragCoord.x + 1.5), 2.0)) == 1;\n"
                          "bool isEvenY = int(mod(floor(gl_FragCoord.y + 1.5), 2.0)) != 1;\n"
                          "if((isEvenX && isEvenY) || (!isEvenX && !isEvenY)) { discard; }\n",
                           ST_SHADER_TEMPLATE[2])) {
        stError(aShadersError);
        return false;
    } else if(!aShaderChessRev.init(*myContext,
                                    ST_SHADER_TEMPLATE[0],
              "bool isEvenX = int(mod(floor(gl_FragCoord.x + 1.5), 2.0)) == 1;\n"
              "bool isEvenY = int(mod(floor(gl_FragCoord.y + 1.5), 2.0)) != 1;\n"
              "if(!((isEvenX && isEvenY) || (!isEvenX && !isEvenY))) { discard; }\n",
              ST_SHADER_TEMPLATE[2])) {
        stError(aShadersError);
        return false;
    }
    myGlPrograms   [DEVICE_CHESSBOARD]->create(*myContext)
                                       .attachShader(*myContext, aVertexShader)
                                       .attachShader(*myContext, aShaderChess)
                                       .link(*myContext);
    myGlProgramsRev[DEVICE_CHESSBOARD]->create(*myContext)
                                       .attachShader(*myContext, aVertexShader)
                                       .attachShader(*myContext, aShaderChessRev)
                                       .link(*myContext);

    /// TODO (Kirill Gavrilov#3) fix shaders
    const StString aShadersRoot = StProcess::getStCoreFolder() + "shaders" + SYS_FS_SPLITTER
                                + ST_OUT_PLUGIN_NAME + SYS_FS_SPLITTER;
    StGLVertexShader stVShaderED("ED control");
    StGLAutoRelease aTmp8(*myContext, stVShaderED);
    if(!stVShaderED.initFile(*myContext, aShadersRoot + VSHADER_ED)) {
        stError(aShadersError);
        return false;
    }

    StGLFragmentShader stFInterlaceOn(myEDIntelaceOn->getTitle());
    StGLAutoRelease aTmp9(*myContext, stFInterlaceOn);
    if(!stFInterlaceOn.initFile(*myContext, aShadersRoot + FSHADER_EDINTERLACE_ON)) {
        stError(aShadersError);
        return false;
    }
    myEDIntelaceOn->create(*myContext)
                   .attachShader(*myContext, stVShaderED)
                   .attachShader(*myContext, stFInterlaceOn)
                   .link(*myContext);

    StGLFragmentShader stFShaderEDOff(myEDOff->getTitle());
    StGLAutoRelease aTmp10(*myContext, stFShaderEDOff);
    if(!stFShaderEDOff.initFile(*myContext, aShadersRoot + FSHADER_ED_OFF)) {
        stError(aShadersError);
        return false;
    }
    myEDOff->create(*myContext)
            .attachShader(*myContext, stVShaderED)
            .attachShader(*myContext, stFShaderEDOff)
            .link(*myContext);

    myVpSizeYOnLoc  = myEDIntelaceOn->getUniformLocation(*myContext, "vpSizeY");
    myVpSizeYOffLoc = myEDOff       ->getUniformLocation(*myContext, "vpSizeY");

    if(myDevice == DEVICE_HINTERLACE_ED) {
        // could be eDimensional shuttered glasses
        myEDTimer.restart(2000000.0);
    }

    // create vertices buffers to draw simple textured quad
    const GLfloat QUAD_VERTICES[4 * 4] = {
         1.0f, -1.0f, 0.0f, 1.0f, // top-right
         1.0f,  1.0f, 0.0f, 1.0f, // bottom-right
        -1.0f, -1.0f, 0.0f, 1.0f, // top-left
        -1.0f,  1.0f, 0.0f, 1.0f  // bottom-left
    };

    const GLfloat QUAD_TEXCOORD[2 * 4] = {
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        0.0f, 1.0f
    };

    myQuadVertBuf    .init(*myContext, 4, 4, QUAD_VERTICES);
    myQuadTexCoordBuf.init(*myContext, 2, 4, QUAD_TEXCOORD);

    return true;
}

void StOutInterlace::processEvents(StMessage_t* theMessages) {
    // process exit from StApplication
    if(theMessages[0].uin == StMessageList::MSG_EXIT
    && (myDevice == DEVICE_HINTERLACE_ED) && myIsEDactive) {
        // disactivate eDimensional shuttered glasses
        myEDTimer.restart();
        myIsEDactive = false;
        while(myEDTimer.getElapsedTime() <= 0.5) {
            stglDraw();
            StThread::sleep(10);
        }
    }

    StWindow::processEvents(theMessages);
    for(size_t anIter = 0; theMessages[anIter].uin != StMessageList::MSG_NULL; ++anIter) {
        switch(theMessages[anIter].uin) {
            case StMessageList::MSG_RESIZE: {
                const StRectI_t aRect = StWindow::getPlacement();
                myVpSizeY = aRect.height();
                if(!StWindow::isFullScreen()) {
                    const StSearchMonitors& aMonitors = StWindow::getMonitors();
                    if(myMonitor.isNull()) {
                        myMonitor = new StMonitor(aMonitors[aRect.center()]);
                    } else if(!myMonitor->getVRect().isPointIn(aRect.center())) {
                        *myMonitor = aMonitors[aRect.center()];
                    }
                    myEDRect.left()   = 0;
                    myEDRect.right()  = myMonitor->getVRect().width();
                    myEDRect.top()    = 0;
                    myEDRect.bottom() = 10;
                    myVpSizeY = 10;
                }
                break;
            }
            case StMessageList::MSG_KEYS: {
                bool* aKeys = ((bool* )theMessages[anIter].data);
                if(aKeys[ST_VK_F1]) {
                    myDevice = DEVICE_HINTERLACE;    aKeys[ST_VK_F1] = false;
                } else if(aKeys[ST_VK_F2]) {
                    myDevice = DEVICE_VINTERLACE;    aKeys[ST_VK_F2] = false;
                } else if(aKeys[ST_VK_F3]) {
                    myDevice = DEVICE_CHESSBOARD;    aKeys[ST_VK_F3] = false;
                } else if(aKeys[ST_VK_F4]) {
                    myDevice = DEVICE_HINTERLACE_ED; aKeys[ST_VK_F4] = false;
                }
                break;
            }
            case StMessageList::MSG_WIN_ON_NEW_MONITOR: {
                const StSearchMonitors& aMonitors = StWindow::getMonitors();
                const StRectI_t  aRect = StWindow::getPlacement();
                const StMonitor& aMon  = aMonitors[aRect.center()];
                myIsMonReversed = false;
                isInterlacedMonitor(aMon, myIsMonReversed);
                break;
            }
        }
    }
}

void StOutInterlace::stglDrawEDCodes() {
    if(myEDTimer.getElapsedTime() > 0.5) {
        StWindow::hide(ST_WIN_SLAVE);
        myIsEDCodeFinished = true;
        return;
    }
    if(!StWindow::isFullScreen()) {
        StWindow::show(ST_WIN_SLAVE);
        StWindow::stglMakeCurrent(ST_WIN_SLAVE);
        myContext->stglResize(myEDRect);
        // clear the screen and the depth buffer
        myContext->core20fwd->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    myContext->core20fwd->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    myContext->core20fwd->glEnable(GL_BLEND);
    if(myIsEDactive) {
        myEDIntelaceOn->use(*myContext);
        if(myVpSizeYOnLoc != -1) {
            // TODO (Kirill Gavrilov#9#) use glViewport instead
            myContext->core20fwd->glUniform1i(myVpSizeYOnLoc, myVpSizeY);
        }
    } else {
        myEDOff->use(*myContext);
        if(myVpSizeYOffLoc != -1) {
            // TODO (Kirill Gavrilov#9#) use glViewport instead
            myContext->core20fwd->glUniform1i(myVpSizeYOffLoc, myVpSizeY);
        }
    }
    // TODO (Kirill Gavrilov#5) use vertex buffer
    myContext->core11->glBegin(GL_QUADS);
        myContext->core11->glVertex2f(-1.0f, -1.0f);
        myContext->core11->glVertex2f( 1.0f, -1.0f);
        myContext->core11->glVertex2f( 1.0f,  1.0f);
        myContext->core11->glVertex2f(-1.0f,  1.0f);
    myContext->core11->glEnd();
    myEDIntelaceOn->unuse(*myContext); // this is global unuse
    myContext->core20fwd->glDisable(GL_BLEND);
    if(!StWindow::isFullScreen()) {
        StWindow::stglSwap(ST_WIN_SLAVE);
    }
}

void StOutInterlace::stglDraw() {
    myFPSControl.setTargetFPS(StWindow::getTargetFps());

    // always draw LEFT view into real screen buffer
    StWindow::stglMakeCurrent(ST_WIN_MASTER);
    myContext->stglResize(StWindow::getPlacement());
    StWindow::signals.onRedraw(ST_DRAW_LEFT);

    if(!StWindow::isStereoOutput() || myIsBroken) {
        if(myToCompressMem) {
            myFrmBuffer->release(*myContext);
        }

        if(myDevice == DEVICE_HINTERLACE_ED) {
            // EDimensional disactivation
            // TODO (Kirill Gavrilov#4#) implement logic to sync multiple sView instances
            if(myIsEDCodeFinished) {
                if(myIsStereo) {
                    if(myIsEDactive) {
                        myEDTimer.restart();
                        myIsEDactive = false;
                        myIsEDCodeFinished = false;
                    }
                    myIsStereo = false;
                }
            }
            stglDrawEDCodes();
        }

        // decrease FPS to target by thread sleeps
        myFPSControl.sleepToTarget();
        StWindow::stglSwap(ST_WIN_MASTER);
        ++myFPSControl;
        return;
    }

    // reverse L/R according to window position
    bool isPixelReverse = false;
    const StRectI_t aWinRect = StWindow::getPlacement();

    // resize FBO
    if(!myFrmBuffer->initLazy(*myContext, aWinRect.width(), aWinRect.height())) {
        stError(StString(ST_OUT_PLUGIN_NAME) + " Plugin, Failed to init Frame Buffer");
        myIsBroken = true;
        return;
    }

    // odd vertically?
    if(!StWindow::isFullScreen() && aWinRect.bottom() % 2 == 1) {
        switch(myDevice) {
            case DEVICE_CHESSBOARD:
            case DEVICE_HINTERLACE:
            case DEVICE_HINTERLACE_ED:
                isPixelReverse = !isPixelReverse; break;
        }
    }

    // odd horizontally?
    if(!StWindow::isFullScreen() && aWinRect.left() % 2 == 1) {
        switch(myDevice) {
            case DEVICE_CHESSBOARD:
            case DEVICE_VINTERLACE:
                isPixelReverse = !isPixelReverse; break;
        }
    }

    // known monitor model with reversed rows order?
    if(myIsMonReversed) {
        isPixelReverse = !isPixelReverse;
    }

    // reversed by user?
    if(params.ToReverse->getValue()) {
        isPixelReverse = !isPixelReverse;
    }

    // reduce viewport to avoid additional aliasing of narrow lines
    GLfloat aDX = GLfloat(myFrmBuffer->getVPSizeX()) / GLfloat(myFrmBuffer->getSizeX());
    GLfloat aDY = GLfloat(myFrmBuffer->getVPSizeY()) / GLfloat(myFrmBuffer->getSizeY());
    StArray<StGLVec2> aTCoords(4);
    aTCoords[0] = StGLVec2(aDX,  0.0f);
    aTCoords[1] = StGLVec2(aDX,  aDY);
    aTCoords[2] = StGLVec2(0.0f, 0.0f);
    aTCoords[3] = StGLVec2(0.0f, aDY);
    myQuadTexCoordBuf.init(*myContext, aTCoords);

    // draw into virtual frame buffer
    GLint aVPort[4]; // real window viewport
    myContext->core20fwd->glGetIntegerv(GL_VIEWPORT, aVPort);
    myFrmBuffer->setupViewPort(*myContext); // we set TEXTURE sizes here
    myFrmBuffer->bindBuffer(*myContext);
        StWindow::signals.onRedraw(ST_DRAW_RIGHT);
    myFrmBuffer->unbindBuffer(*myContext);
    myContext->core20fwd->glViewport(aVPort[0], aVPort[1], aVPort[2], aVPort[3]);

    myContext->core20fwd->glDisable(GL_DEPTH_TEST);
    myContext->core20fwd->glDisable(GL_BLEND);

    myFrmBuffer->bindTexture(*myContext);
    const StHandle<StProgramFB>& aProgram = isPixelReverse ? myGlProgramsRev[myDevice] : myGlPrograms[myDevice];
    aProgram->use(*myContext);
    myQuadVertBuf.bindVertexAttrib(*myContext, ST_VATTRIB_VERTEX);
    myQuadTexCoordBuf.bindVertexAttrib(*myContext, ST_VATTRIB_TCOORD);

    myContext->core20fwd->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    myQuadTexCoordBuf.unBindVertexAttrib(*myContext, ST_VATTRIB_TCOORD);
    myQuadVertBuf.unBindVertexAttrib(*myContext, ST_VATTRIB_VERTEX);
    aProgram->unuse(*myContext);
    myFrmBuffer->unbindTexture(*myContext);

    if(myDevice == DEVICE_HINTERLACE_ED) {
        // EDimensional activation
        if(myIsEDCodeFinished) {
            if(!myIsStereo) {
                if(!myIsEDactive) {
                    myEDTimer.restart();
                    myIsEDactive = true;
                    myIsEDCodeFinished = false;
                }
                myIsStereo = true;
            }
        }
        stglDrawEDCodes();
    }

    // decrease FPS to target by thread sleeps
    myFPSControl.sleepToTarget();
    StWindow::stglSwap(ST_WIN_MASTER);
    ++myFPSControl;
}

void StOutInterlace::doVSync(const bool theValue) {
    if(myContext.isNull()) {
        return;
    }

    StWindow::stglMakeCurrent(ST_WIN_MASTER);
    myContext->stglSetVSync(theValue ? StGLContext::VSync_ON : StGLContext::VSync_OFF);
}

void StOutInterlace::doSetBindToMonitor(const bool theValue) {
    if(!theValue
    || StWindow::isFullScreen()) {
        return;
    }

    const StSearchMonitors& aMonitors = StWindow::getMonitors();
    StRectI_t aRect = StWindow::getPlacement();
    StMonitor aMon  = aMonitors[aRect.center()];
    StHandle<StMonitor> anInterlacedMon = StOutInterlace::getHInterlaceMonitor(aMonitors, myIsMonReversed);
    if(!anInterlacedMon.isNull()
    && !isInterlacedMonitor(aMon, myIsMonReversed)) {
        int aWidth  = aRect.width();
        int aHeight = aRect.height();
        aRect.left()   = anInterlacedMon->getVRect().left() + 256;
        aRect.right()  = aRect.left() + aWidth;
        aRect.top()    = anInterlacedMon->getVRect().top() + 256;
        aRect.bottom() = aRect.top() + aHeight;
        StWindow::setPlacement(aRect);
    }
}
