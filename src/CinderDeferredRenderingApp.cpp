// What is this?

// A Cinder app that utilizes a deferred rendering engine
// to render lights and SSAO. There is also point-light
// shadow-mapping (heavy GPU cost though)

// TODO: lots of optimization to do here yet

// Main reason this will be slow will be GPU pipes and
// RAM as deferred rendering + shadow-mapping uses a huge
// amount if VRAM (one of its disadvantages)
// Deferred Rendering ADVANTAGE: tons of dynamic point lights (w/o shadows) possible, if not at GPU limits already anyhow

// Inspiration and shader base from
// http://www.gamedev.net/page/resources/_/technical/graphics-programming-and-theory/image-space-lighting-r2644
// and
// http://http.developer.nvidia.com/GPUGems2/gpugems2_chapter09.html

// Tips to get better framerates:
// - adjust ap window size
// - go to initFBO's and lower resolution of maps
// - turn off number shadow-mapped lights by setting last parameter of LIGHT_PS constructor to false
// - Tested on Macbook Pro 2010 Mountain Lion ~30fps
// - Tested on HP620 Windows 7 ~60-70 fps

// Controls

// Key 0: Show final composed scene
// Keys 1-9: Toggle through deferred layers (depth, colour, normal, shadows etc.)

// Alt+Left Mouse Button: Rotate camera
// Alt+Right Mouse Button: Dolly camera

// Cursor Keys: Move Selected Light (with shift for up and down movement)
// +/-: Switch between "selected" light

#include <functional>

#include "cinder/app/AppBasic.h"
#include "cinder/gl/gl.h"
#include "cinder/Camera.h"
#include "cinder/gl/GlslProg.h"
#include "cinder/gl/Texture.h"
#include "cinder/ImageIo.h"
#include "cinder/MayaCamUI.h"
#include "cinder/params/Params.h"
#include "cinder/Rand.h"
#include "cinder/TriMesh.h"
#include "cinder/ObjLoader.h"

#include "CubeShadowMap.h"
#include "DeferredRenderer.h"

#include "Resources.h"

using namespace ci;
using namespace std;




class CinderDeferredRenderingApp : public AppBasic {
	const int APP_RES_HORIZONTAL = 1024;
	const int APP_RES_VERTICAL = 768;

    const int SCENE_SIZE = 3000;
	const int NUM_LIGHTS = 500;

	const Vec3f CAM_POSITION_INIT = Vec3f(-14.0f, 7.0f, -14.0f);
	const Vec3f LIGHT_POSITION_INIT = Vec3f(3.0f, 1.5f, 0.0f);
	
public:

    CinderDeferredRenderingApp();
    virtual ~CinderDeferredRenderingApp();
    void prepareSettings(Settings* settings);
    
    void setup();
    void update();
    void draw();
    
    void mouseDown(MouseEvent event);
	void mouseDrag(MouseEvent event);
	void mouseWheel(MouseEvent event);
    void keyDown(app::KeyEvent event);
    
    void drawShadowCasters(gl::GlslProg* shader) const;
    void drawNonShadowCasters(gl::GlslProg* shader) const;
    
private:
	
    cinder::params::InterfaceGl mParams;
    bool mShowParams = true;
    float mFramerate = 0;
	
    DeferredRenderer::RenderMode mRenderMode = DeferredRenderer::SHOW_FINAL_VIEW;

	bool mAnimate = false;
	bool mShadows = false;
	bool mSSAO = false;
	
    DeferredRenderer mRenderer;

    MayaCamUI mCamera;
	
	gl::Texture mTexture;
	cinder::TriMesh mMesh;
    
    int mCurrentLightIndex = 0;
};


CinderDeferredRenderingApp::CinderDeferredRenderingApp() {}
CinderDeferredRenderingApp::~CinderDeferredRenderingApp() {}

#pragma mark - lifecycle functions

void CinderDeferredRenderingApp::prepareSettings(Settings* settings) {
	settings->setWindowSize(APP_RES_HORIZONTAL, APP_RES_VERTICAL);
    settings->setBorderless(false);
	settings->setFrameRate(1000.0f);
	settings->setResizable(false);
    settings->setFullScreen(false);
}

void CinderDeferredRenderingApp::setup() {
	//gl::disableVerticalSync(); //so I can get a true representation of FPS (if higher than 60 anyhow :/)
    
	mParams = params::InterfaceGl("3D_Scene_Base", Vec2i(225, 125));
	mParams.addParam("Framerate", &mFramerate, "", true);
	mParams.addParam("Show/Hide", &mShowParams, "key=x");
	mParams.addText("RenderMode: 0...9");
	mParams.addSeparator();
	mParams.addParam("Ambient Occlusion (SSAO)", &mSSAO, "key=a");
	mParams.addParam("Shadows", &mShadows, "key=s");
	mParams.addParam("Disco Mode", &mAnimate, "key=d");
    mParams.addParam("Selected Light Index", &mCurrentLightIndex);

	// set up camera
    CameraPersp initialCam;
	initialCam.setPerspective(45.0f, getWindowAspectRatio(), 0.1, 10000);
    initialCam.lookAt(CAM_POSITION_INIT * 1.5f, Vec3f::zero(), Vec3f(0, 1, 0));
    initialCam.setCenterOfInterestPoint(Vec3f::zero());
	mCamera.setCurrentCam(initialCam);
	
    // create functions pointers to send to deferred renderer
	auto shadowCasterFunc = std::bind(&CinderDeferredRenderingApp::drawShadowCasters, this, std::placeholders::_1);
	auto nonShadowCasterFunc = std::bind(&CinderDeferredRenderingApp::drawNonShadowCasters, this, std::placeholders::_1);
	
	mRenderer.setup(shadowCasterFunc, nonShadowCasterFunc, &mCamera, Vec2i(APP_RES_HORIZONTAL, APP_RES_VERTICAL), 1024);
    
    // have these point lights cast shadows
    mRenderer.addLight(Vec3f(-2.0f, 4.0f, 6.0f), Color(0.10f, 0.69f, 0.93f), true);
    mRenderer.addLight(Vec3f(4.0f, 6.0f, -4.0f), Color(0.94f, 0.15f, 0.23f), true);
    mRenderer.addLight(Vec3f(-6.0f, 8.0f, -4.0f), Color(0.14f, 0.95f, 0.23f), true);
    
    // add a bunch of lights that don't cast shadows
    for (int i = 0; i < NUM_LIGHTS; ++i) {
        int colorIndex = Rand::randInt(5);
        Color color;
        switch (colorIndex) {
            case 0:
                color = Color(0.99f, 0.67f, 0.23f); // orange
                break;
            case 1:
                color = Color(0.97f, 0.24f, 0.85f); // pink
                break;
            case 2:
                color = Color(0.00f, 0.93f, 0.30f); // green
                break;
            case 3:
                color = Color(0.98f, 0.96f, 0.32f); // yellow
                break;
            case 4:
                color = Color(0.10f, 0.69f, 0.93f); // blue
                break;
            case 5:
                color = Color(0.94f, 0.15f, 0.23f); // red
                break;
        };
        mRenderer.addLight(Vec3f(Rand::randFloat(-1000, 1000), Rand::randFloat(0, 100), Rand::randFloat(-1000, 1000)), color);
    }

	// load texture for earth sphere (note: will be upside down)
	mTexture = gl::Texture(loadImage(loadResource(RES_EARTH_TEX)));
	ObjLoader loader(loadResource(RES_NW_OBJ));
	loader.load(&mMesh);
}

void CinderDeferredRenderingApp::update() {
	mFramerate = getAverageFps();
}

void CinderDeferredRenderingApp::draw() {
	if (mAnimate) {
		double seconds = getElapsedSeconds();
		auto lights = mRenderer.getLights();
		for (int i = 0; i < lights.size(); ++i) {
			if (lights[i]->isShadowCaster()) continue;
			Vec3f pos = lights[i]->getPosition();
			pos.rotateY(0.00001 * i * (i % 2 ? -1 : 1));
			lights[i]->setPosition(pos);
			
			lights[i]->setBrightness(PointLight::LIGHT_BRIGHTNESS_DEFAULT + 2000 * sin(2 * seconds + i) * sin(2 * seconds + i));
		}
	}
    mRenderer.render(mRenderMode, mShadows, mSSAO);
	if (mShowParams)
		mParams.draw();
}

void CinderDeferredRenderingApp::mouseDown(MouseEvent event) {
	mCamera.mouseDown(event.getPos());
}

void CinderDeferredRenderingApp::mouseDrag(MouseEvent event) {
	mCamera.mouseDrag(event.getPos(), event.isLeftDown(), event.isMiddleDown(), event.isRightDown());
}

void CinderDeferredRenderingApp::mouseWheel(MouseEvent event) {
	// XXX tbd
}

void CinderDeferredRenderingApp::keyDown(KeyEvent event) {
	if (mRenderer.getLights().empty())
		return;

    float lightMoveInc = 0.1f;

	switch (event.getCode()) {
		case KeyEvent::KEY_0:
			mRenderMode = DeferredRenderer::SHOW_FINAL_VIEW;
			break;
		case KeyEvent::KEY_1:
			mRenderMode = DeferredRenderer::SHOW_DIFFUSE_VIEW;
			break;
		case KeyEvent::KEY_2:
			mRenderMode = DeferredRenderer::SHOW_NORMALMAP_VIEW;
			break;
		case KeyEvent::KEY_3:
			mRenderMode = DeferredRenderer::SHOW_DEPTH_VIEW;
			break;
        case KeyEvent::KEY_4:
			mRenderMode = DeferredRenderer::SHOW_POSITION_VIEW;
			break;
        case KeyEvent::KEY_5:
			mRenderMode = DeferredRenderer::SHOW_ATTRIBUTE_VIEW;
			break;
        case KeyEvent::KEY_6:
			mRenderMode = DeferredRenderer::SHOW_SSAO_VIEW;
			break;
        case KeyEvent::KEY_7:
			mRenderMode = DeferredRenderer::SHOW_SSAO_BLURRED_VIEW;
			break;
        case KeyEvent::KEY_8:
			mRenderMode = DeferredRenderer::SHOW_LIGHT_VIEW;
            break;
        case KeyEvent::KEY_9:
			mRenderMode = DeferredRenderer::SHOW_SHADOWS_VIEW;
			break;

		// change which light you want to control
        case KeyEvent::KEY_COMMA:
			mCurrentLightIndex--;
			if (mCurrentLightIndex < 0)
				mCurrentLightIndex = mRenderer.getLights().size() - 1;
            break;
        case KeyEvent::KEY_PERIOD:
			mCurrentLightIndex++;
			if (mCurrentLightIndex > mRenderer.getLights().size() - 1)
				mCurrentLightIndex = 0;
            break;
			
		// move selected light
		case KeyEvent::KEY_UP:
			if (event.isShiftDown()) {
				mRenderer.getLights().at(mCurrentLightIndex)->setPosition(mRenderer.getLights().at(mCurrentLightIndex)->getPosition() + Vec3f(0, lightMoveInc, 0));
			} else {
				mRenderer.getLights().at(mCurrentLightIndex)->setPosition(mRenderer.getLights().at(mCurrentLightIndex)->getPosition() + Vec3f(0, 0, lightMoveInc));
			}
			break;
		case KeyEvent::KEY_DOWN:
			if (event.isShiftDown()) {
				mRenderer.getLights().at(mCurrentLightIndex)->setPosition(mRenderer.getLights().at(mCurrentLightIndex)->getPosition() + Vec3f(0, -lightMoveInc, 0));
			} else {
				mRenderer.getLights().at(mCurrentLightIndex)->setPosition(mRenderer.getLights().at(mCurrentLightIndex)->getPosition() + Vec3f(0, 0, -lightMoveInc));
			}
			break;
		case KeyEvent::KEY_LEFT:
			mRenderer.getLights().at(mCurrentLightIndex)->setPosition(mRenderer.getLights().at(mCurrentLightIndex)->getPosition() + Vec3f(lightMoveInc, 0, 0));
			break;
		case KeyEvent::KEY_RIGHT:
			mRenderer.getLights().at(mCurrentLightIndex)->setPosition(mRenderer.getLights().at(mCurrentLightIndex)->getPosition() + Vec3f(-lightMoveInc, 0, 0));
            break;

        case KeyEvent::KEY_ESCAPE:
            exit(1);
			break;
		default:
			break;
	}
}

#pragma mark - render functions

void CinderDeferredRenderingApp::drawShadowCasters(gl::GlslProg* shader) const {
	// render some test objects
	if (shader != nullptr) {
		shader->uniform("useTexture", 1);
		shader->uniform("tex", 0);
		mTexture.bind(0);
	}

	glColor3f(1, 0, 0);
	gl::drawSphere(Vec3f(-1.0, 0.0, -1.0), 1.0f, 30);

	if (shader != nullptr) {
		shader->uniform("useTexture", 0);
		mTexture.unbind(0);
	}

	glColor3f(0, 1, 0);
	gl::drawCube(Vec3f(1.0f, 0.0f, 1.0f), Vec3f(2.0f, 2.0f, 2.0f));

	glColor3f(1, 0, 1);
	gl::drawCube(Vec3f(0.0f, 0.0f, 4.5f), Vec3f(1.0f, 2.0f, 1.0f));

	glColor3f(1, 1, 0);
	gl::drawCube(Vec3f(3.0f, 0.0f, -1.5f), Vec3f(1.0f, 3.0f, 1.0f));

	glColor3f(1, 0, 1);
	gl::pushMatrices();
	glTranslatef(-2.0f, -0.7f, 2.0f);
	glRotated(60.0f, 1, 1, 1);
	gl::drawTorus(1.0f, 0.3f, 32, 64);
	gl::popMatrices();
	
	gl::pushMatrices();
	glTranslatef(6.0f, 0.0f, 4.0f);
	gl::draw(mMesh);
	gl::popMatrices();
}

void CinderDeferredRenderingApp::drawNonShadowCasters(gl::GlslProg* shader) const {
	if (mAnimate) {
		if (shader != nullptr) {
			shader->uniform("useTexture", 1.0f);
			shader->uniform("tex", 0);
			mTexture.bind(0);
		}
	
		gl::pushMatrices();
		glRotated(20 * getElapsedSeconds(), 0, 1, 0);
		glTranslatef(500, 50, 0);
		gl::drawSphere(Vec3f(0, 0, 0), 50.0f, 30);
		gl::popMatrices();
	
		if (shader != nullptr) {
			shader->uniform("useTexture", 0.0f);
			mTexture.unbind(0);
		}
	}

	// a plane to capture shadows (though it won't cast any itself)
	glColor3ub(255, 255, 255);
	glNormal3f(0.0f, 1.0f, 0.0f);
	glBegin(GL_QUADS);
	glVertex3i(SCENE_SIZE, -2,-SCENE_SIZE);
	glVertex3i(-SCENE_SIZE, -2,-SCENE_SIZE);
	glVertex3i(-SCENE_SIZE, -2, SCENE_SIZE);
	glVertex3i(SCENE_SIZE, -2, SCENE_SIZE);
	glEnd();
}

CINDER_APP_BASIC(CinderDeferredRenderingApp, RendererGl)
