#pragma once
#include <string>
#include <fstream>
#include <vector>
#include <cv.h>
#include <cxcore.h>
#include <highgui.h>
using namespace std;
using namespace cv;

struct CameraModel
{
public:
	cv::Mat rotation; //3*3
	cv::Mat translation; //1*3
	double focallength; 
	double distortX;
	double distortY;

	CameraModel getMedian(CameraModel& cam)
	{
		CameraModel newCM;
		newCM.rotation = (rotation + cam.rotation) / 2;
		newCM.translation = (translation + cam.translation) / 2;
		newCM.focallength = (focallength + cam.focallength) /2;
		newCM.distortX = (distortX + cam.distortX) / 2;
		newCM.distortY = (distortY + cam.distortY) / 2;
		return newCM;
	}
};

struct FeatureOneView
{
	int cameraIndex;
	int siftIndex;
	cv::Point2f position2D;
};

struct OneFeatureInWholeScene
{
	cv::Scalar sceneColor;
	cv::Point3f position3D;
	int numOfVisibelCam;
	vector<FeatureOneView> featInAllViews;
};

class BundlerFileLoader
{
public:
	int viewNum;
	int featNum;
	vector<string> viewImgFileName;
	vector<OneFeatureInWholeScene> allFeats;
	vector<CameraModel> allCameras;	//index by the camera ID, not in frame sequence
	vector<cv::Mat> allImgs;
public:
	BundlerFileLoader();
	int getCameraByImgName(string& imgName);
	BundlerFileLoader(string& cameraFileName, string& bundlerFileName);
	void init(string& cameraFileName, string& bundlerFileName);
	~BundlerFileLoader();
};

