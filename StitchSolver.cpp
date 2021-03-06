#include "StitchSolver.h"
#include "CV_Util2.h"
#include <direct.h>
#include "Logger.h"
#include "Configer.h"
#include <fstream>

using namespace std;

StitchSolver::StitchSolver()
{
	string baseFolder, folder, No, Cmvs;
	Configer::getConfiger()->getString("input", "baseFolder", baseFolder);
	Configer::getConfiger()->getString("input", "folder", folder);
	Configer::getConfiger()->getString("input", "No", No);
	Configer::getConfiger()->getString("input", "Cmvs", Cmvs);
	
	REPORT(baseFolder);
	REPORT(folder);
	REPORT(No);
	REPORT(Cmvs);

	oriFolder = baseFolder + folder + string("\\") + No + string("\\origin");
	maskFolder = baseFolder + folder + string("\\") + No + string("\\mask");
	fgFolder = baseFolder + folder + string("\\") + No + string("\\fg");
	bgFolder = baseFolder + folder + string("\\") + No + string("\\bg");
	siftFolder = baseFolder + folder + string("\\") + No + string("\\sift");
	outFolder = baseFolder + folder + string("\\") + No + string("\\out");
	cameraList = baseFolder + folder + string("\\") + No + string("\\") + Cmvs + string("\\00\\cameras_v2.txt");
	bundlerRes = baseFolder + folder + string("\\") + No + string("\\") + Cmvs + string("\\00\\bundle.rd.out");

	Configer::getConfiger()->getInt("warp", "GridX", GridX);
	Configer::getConfiger()->getInt("warp", "GridY", GridY);
	REPORT(GridX);
	REPORT(GridY);


	//create folder
	_mkdir(fgFolder.c_str());
	_mkdir(bgFolder.c_str());
	_mkdir(siftFolder.c_str());
	_mkdir(outFolder.c_str());
	_mkdir(join_path("warpedFG1").c_str());
	_mkdir(join_path("warpedFG2").c_str());
	_mkdir(join_path("warpedBG").c_str());
	_mkdir(join_path("match").c_str());
	_mkdir(join_path("mesh").c_str());

}

void StitchSolver::prepareForBundler()
{
	// Handle mask before recovering
	SIFTHandle::updateSIFTfolder(oriFolder, maskFolder, fgFolder, bgFolder, siftFolder);

}

void StitchSolver::loadReconstruction()
{
	//read reconstructed results of bundler
	recover.sfmLoader.init(cameraList, bundlerRes);
	
	//read all file names 
	recover.getFilePaths(maskFolder, fgFolder);

	recover.mapCam2Frame();
	
	//average left and right camera to get new camera
	recover.createNewCamPath();

	FrameH = recover.FrameH;
	FrameW = recover.FrameW;
	
	//set left and right frame correspondence 
	int testFrameNum = 30;
	Configer::getConfiger()->getInt("input", "testFrameNum", testFrameNum);
	for(int i = 0; i < testFrameNum; i++)
		recover.matchedFramesID.push_back(pair<int, int>(i, i));
	recover.cam1Num = recover.cam2Num = testFrameNum;

	//note frames whose foreground is not complete
	for(int i = 34; i < 78; i++)
		recover.lackFramesPair.push_back(i);

	//reconstruct foreground feature points 3D
	bool warpFG;
	Configer::getConfiger()->getBool("input", "warpFG", warpFG);
	if(warpFG)
		recover.getForeGround3DAllFrames();
	
	recover.getBackGround3DAllFrames();

}

void StitchSolver::warpFGOnMesh(bool isSequence1)
{

	LOG << "Warping foreground based on mesh...\n\n";

	vector<Mat_<Vec2f>> deformedMesh;
	for(int i = 0; i < recover.matchedFramesID.size(); i++)
	{
		warpFGOnMesh(i, isSequence1, deformedMesh);
		LOG << "Calculating deformed mesh for " << i << "th frame finished\n\n";
	}

	//interpolate meshes for incomplete foreground frames
	fillMissedMesh(isSequence1, deformedMesh);
	
	//run low-pass filter in deformed meshes
	filterDeformedMesh(deformedMesh);

	warper = Warper(Mesh(FrameW, FrameH, GridX, GridY));
	for(int i = 0; i < recover.matchedFramesID.size(); i++)
	{	
		int frameId1 = recover.matchedFramesID[i].first;
		int frameId2 = recover.matchedFramesID[i].second;
		int idInSFM1 = recover.frame2Cam[make_pair(1, frameId1)];
		int idInSFM2 = recover.frame2Cam[make_pair(2, frameId2)];
		Mat origin, mask;
		if(isSequence1)
		{
			origin = imread(recover.cam1ImgNames[frameId1]);
			mask = imread(recover.cam1MaskNames[frameId1]);
		}
		else
		{
			origin = imread(recover.cam2ImgNames[frameId2]);
			mask = imread(recover.cam2MaskNames[frameId2]);
		}
		Mat warped_o(FrameH, FrameW, CV_8UC3, Scalar(0, 0, 0));
		Mat warped_m(FrameH, FrameW, CV_8UC3, Scalar(0, 0, 0));
		
		//draw deformed mesh
		vector<Point2f> oriPoints;//Here is NULL
		CVUtil::visualizeMeshAndFeatures(origin, warped_o, oriPoints, deformedMesh[i]);
		string baseFolder, folder, No, Cmvs;
		Configer::getConfiger()->getString("input", "baseFolder", baseFolder);
		Configer::getConfiger()->getString("input", "folder", folder);
		Configer::getConfiger()->getString("input", "No", No);
		string meshFolder = baseFolder + folder + string("\\") + No + string("\\out\\mesh\\");
		char num[10];
		sprintf_s(num, "%d", frameId1);
		if (isSequence1)
			imwrite((meshFolder + string(num) + string("_cam1.jpg")), warped_o);
		else
			imwrite((meshFolder + string(num) + string("_cam2.jpg")), warped_o);
		
	//	continue;

		//perform mesh based warping
		warper.warpBilateralInterpolate(origin, deformedMesh[i], warped_o, false, true);
		warper.warpBilateralInterpolate(mask, deformedMesh[i], warped_m, false, true);

		char path_o[100], path_m[100];
		if(isSequence1)
		{
			sprintf_s(path_o, "%s\\warpedFG1\\img%d.jpg", outFolder.c_str(), i);
			sprintf_s(path_m, "%s\\warpedFG1\\mask%d.jpg", outFolder.c_str(), i);
		}
		else
		{
			sprintf_s(path_o, "%s\\warpedFG2\\img%d.jpg", outFolder.c_str(), i);
			sprintf_s(path_m, "%s\\warpedFG2\\mask%d.jpg", outFolder.c_str(), i);
		}
		imwrite(path_o, warped_o);
		imwrite(path_m, warped_m);
		
		LOG << "Warping " << i << "th frame finished\n\n";
	}
}

void StitchSolver::warpFGOnMesh(int frameMatchId, bool isSequence1, vector<Mat_<Vec2f>>& deformedMesh)
{
	int frameId1 = recover.matchedFramesID[frameMatchId].first;
	int frameId2 = recover.matchedFramesID[frameMatchId].second;
	int idInSFM1 = recover.frame2Cam[make_pair(1, frameId1)];
	int idInSFM2 = recover.frame2Cam[make_pair(2, frameId2)];

	vector<Point2f> oriPoints, tgtPoints;
	vector<Point3f> scnPoints;

	//fill in foreground points

		//if there is no foreground in one frame, continue
		if( std::find(recover.lackFramesPair.begin(), recover.lackFramesPair.end(), frameMatchId) != recover.lackFramesPair.end() )
		{
			Mat_<Vec2f> noneMesh;
			deformedMesh.push_back(noneMesh);
			return;
		}
		vector<scenePointOnPair>& ScnPoints = recover.allForeGroundScenePoints[frameMatchId];
		
		for(int k = 0; k < 1; k++)
		{
			for(unsigned i = 0; i < ScnPoints.size(); i++)
			{
				if(isSequence1)
				{
					oriPoints.push_back(ScnPoints[i].pos2D_1);
					tgtPoints.push_back(ScnPoints[i].pos2D_2);
				}
				else
				{
					oriPoints.push_back(ScnPoints[i].pos2D_2);
					tgtPoints.push_back(ScnPoints[i].pos2D_1);
				}
				scnPoints.push_back(ScnPoints[i].scenePos);
			}
		}
	LOG << "Feature points after filling FG: " << (int)oriPoints.size() << '\n';

	generator = ViewGenerator(oriPoints, tgtPoints, scnPoints, FrameW, FrameH, GridX, GridY);
	generator.getNewFeaturesPos(recover.sfmLoader.allCameras[idInSFM1].getMedian(recover.sfmLoader.allCameras[idInSFM2]));
	generator.getNewMesh();

	deformedMesh.push_back(generator.deformedMesh);
}

void StitchSolver::warpBGOnMesh()
{

	LOG << "Warping background based on mesh...\n\n";

	vector<Mat_<Vec2f>> deformedMesh1, deformedMesh2;
	for(int i = 0; i < recover.matchedFramesID.size(); i++)
	{
		warpBGOnMesh(i, true, deformedMesh1);
		warpBGOnMesh(i, false, deformedMesh2);
		LOG << "Calculating deformed mesh for " << i << "th frame finished\n\n";
	}

	//run filter in deformed meshes
	filterDeformedMesh(deformedMesh1);
	filterDeformedMesh(deformedMesh2);

	warper = Warper(Mesh(FrameW, FrameH, GridX, GridY));
	for(int i = 0; i < recover.matchedFramesID.size(); i++)
	{	
		int frameId1 = recover.matchedFramesID[i].first;
		int frameId2 = recover.matchedFramesID[i].second;
		int idInSFM1 = recover.frame2Cam[make_pair(1, frameId1)];
		int idInSFM2 = recover.frame2Cam[make_pair(2, frameId2)];
		
		Mat origin1, origin2;
		Mat mask1, mask2;
		origin1 = imread(recover.cam1ImgNames[frameId1]);
		origin2 = imread(recover.cam2ImgNames[frameId2]);
		mask1 = imread(recover.cam1MaskNames[frameId1], 1);
		mask2 = imread(recover.cam2MaskNames[frameId2], 1);
		Mat warped1(FrameH*2, FrameW*2, CV_8UC3, Scalar(0, 0, 0));
		Mat warped2(FrameH*2, FrameW*2, CV_8UC3, Scalar(0, 0, 0));


		Mat warpedMask1(FrameH * 2, FrameW * 2, CV_8UC3, Scalar(0, 0, 0));
		Mat warpedMask2(FrameH * 2, FrameW * 2, CV_8UC3, Scalar(0, 0, 0));
		
		//draw deformed mesh
		/*
		vector<Point2f> oriPoints;//Here is NULL
		CVUtil::visualizeMeshAndFeatures(origin, warped, oriPoints, deformedMesh[i]);
		string baseFolder, folder, No, Cmvs;
		Configer::getConfiger()->getString("input", "baseFolder", baseFolder);
		Configer::getConfiger()->getString("input", "folder", folder);
		Configer::getConfiger()->getString("input", "No", No);
		string meshFolder = baseFolder + folder + string("\\") + No + string("\\out\\mesh\\");
		char num[10];
		sprintf_s(num, "%d", frameId1);
		imwrite((meshFolder + string(num) + string(".jpg")), warped);
		*/

		//perform mesh based warping
		warper.warpBilateralInterpolate(origin1, deformedMesh1[i], warped1);
		warper.warpBilateralInterpolate(origin2, deformedMesh2[i], warped2);
		warper.warpBilateralInterpolate(mask1, deformedMesh1[i], warpedMask1);
		warper.warpBilateralInterpolate(mask2, deformedMesh2[i], warpedMask2);

		//Todo: blend two backgrounds

		Mat warped(FrameH*2, FrameW*2, CV_8UC3, Scalar(0, 0, 0));
		char path[100];
		sprintf_s(path, "%s\\warpedBG\\left_img%d.jpg", outFolder.c_str(), i);
		imwrite(path, warped1);
		sprintf_s(path, "%s\\warpedBG\\right_img%d.jpg", outFolder.c_str(), i);
		imwrite(path, warped2);


		sprintf_s(path, "%s\\warpedBG\\left_mask%d.jpg", outFolder.c_str(), i);
		imwrite(path, warpedMask1);
		sprintf_s(path, "%s\\warpedBG\\right_mask%d.jpg", outFolder.c_str(), i);
		imwrite(path, warpedMask2);
		
		LOG << "Warping " << i << "th frame finished\n\n";
	}
}

void StitchSolver::warpBGOnMesh(int frameMatchId, bool isSequence1, vector<Mat_<Vec2f>>& deformedMesh)
{
	int frameId1 = recover.matchedFramesID[frameMatchId].first;
	int frameId2 = recover.matchedFramesID[frameMatchId].second;
	int idInSFM1 = recover.frame2Cam[make_pair(1, frameId1)];
	int idInSFM2 = recover.frame2Cam[make_pair(2, frameId2)];

	vector<Point2f> oriPoints, tgtPoints;
	vector<Point3f> scnPoints;

	//fill in foreground points
	/*
		//if there is no foreground in one frame, continue
		if( std::find(recover.lackFramesPair.begin(), recover.lackFramesPair.end(), frameMatchId) != recover.lackFramesPair.end() )
		{
			Mat_<Vec2f> noneMesh;
			deformedMesh.push_back(noneMesh);
			return;
		}
		vector<scenePointOnPair>& ScnPoints = recover.allForeGroundScenePoints[frameMatchId];
		
		for(int k = 0; k < 1; k++)
		{
			for(unsigned i = 0; i < ScnPoints.size(); i++)
			{
				if(isSequence1)
				{
					oriPoints.push_back(ScnPoints[i].pos2D_1);
					tgtPoints.push_back(ScnPoints[i].pos2D_2);
				}
				else
				{
					oriPoints.push_back(ScnPoints[i].pos2D_2);
					tgtPoints.push_back(ScnPoints[i].pos2D_1);
				}
				scnPoints.push_back(ScnPoints[i].scenePos);
			}
		}
	LOG << "Feature points after filling FG: " << (int)oriPoints.size() << '\n';
	*/

	
	//fill in background points
		vector<scenePoint> BGScnPointsCam1 = recover.allBackGroundPointsCam1[frameId1];
		vector<scenePoint> BGScnPointsCam2 = recover.allBackGroundPointsCam2[frameId2];
		for(unsigned i = 0; i < BGScnPointsCam1.size(); i++)
		{
			if(isSequence1)
			{
				oriPoints.push_back(BGScnPointsCam1[i].pos2D);
				scnPoints.push_back(BGScnPointsCam1[i].scenePos);
			}
			else
				tgtPoints.push_back(BGScnPointsCam1[i].pos2D);
		}
		for(unsigned i = 0; i < BGScnPointsCam2.size(); i++)
		{
			if(!isSequence1)
			{
				oriPoints.push_back(BGScnPointsCam2[i].pos2D);
				scnPoints.push_back(BGScnPointsCam2[i].scenePos);
			}
			else
				tgtPoints.push_back(BGScnPointsCam2[i].pos2D);
		}
	LOG << "Feature points after filling BG: " << (int)oriPoints.size() << '\n';

	generator = ViewGenerator(oriPoints, tgtPoints, scnPoints, FrameW, FrameH, GridX, GridY);
	//generator.getNewFeaturesPos(recover.sfmLoader.allCameras[recover.frame2Cam[make_pair(2, frameId2)]]);
	generator.getNewFeaturesPos(recover.sfmLoader.allCameras[idInSFM1].getMedian(recover.sfmLoader.allCameras[idInSFM2]));
	generator.getNewMesh();

	deformedMesh.push_back(generator.deformedMesh);
}

void StitchSolver::fillMissedMesh(bool isSequence1, vector<Mat_<Vec2f>>& deformedMesh)
{
	LOG << "Fill missing deformed meshes...\n\n";

	//fill missing mesh
	for(int j = 0; j < recover.lackFramesPair.size(); j++)
	{
		int id = recover.lackFramesPair[j];
		int missingId = isSequence1? recover.matchedFramesID[id].first : recover.matchedFramesID[id].second;
		int last = missingId-1, next = missingId+1;
		int i = j-1;
		while( i >= 0 )
		{
			id = recover.lackFramesPair[i];
			int l = isSequence1? recover.matchedFramesID[id].first : recover.matchedFramesID[id].second;
			if(l != last)
				break;
			else
			{
				last --;
				i --;
			}
		}
		i = j+1;
		while( i < recover.lackFramesPair.size() )
		{
			id = recover.lackFramesPair[i];
			int n = isSequence1? recover.matchedFramesID[id].first : recover.matchedFramesID[id].second;
			if(n != next)
				break;
			else
			{
				next ++;
				i ++;
			}
		}

		Mat_<Vec2f> mesh(deformedMesh[0].rows, deformedMesh[0].cols, Vec2f(0, 0));
		REPORT(missingId);
		REPORT(last);
		REPORT(next);
		float span = next - last;
		mesh = deformedMesh[last] * (next-missingId)/span + deformedMesh[next] * (missingId-last)/span;
		deformedMesh[missingId] = mesh;
	}
}

void StitchSolver::filterDeformedMesh(vector<Mat_<Vec2f>>& deformedMesh)
{
	//Median filter
	int iteration = 3;
	int step = 1;
	Configer::getConfiger()->getInt("deformedMesh", "iteration", iteration);
	Configer::getConfiger()->getInt("deformedMesh", "step", step);

	for(int k = 0; k < iteration; k++)
	{
		for(int i = 0; i < deformedMesh.size(); i++)
		{
			Mat_<Vec2f> mesh(deformedMesh[0].rows, deformedMesh[0].cols, Vec2f(0, 0));
			for(int j = -step; j <= step; j++)
			{
				if(i+j < 0)
					mesh += deformedMesh.front();
				else if(i+j > (int)deformedMesh.size()-1)
					mesh += deformedMesh.back();
				else
					mesh += deformedMesh[i+j];
			}
			deformedMesh[i] = mesh / (2*step+1);
		}
	}
}

void StitchSolver::warpFgHomo()
{
	fgWarper = ForegroundWarper(FrameW, FrameH);

	for(int i = 0; i < recover.matchedFramesID.size(); i++)
	{
		int id1 = recover.matchedFramesID[i].first;
		int id2 = recover.matchedFramesID[i].second;

		Mat origin1 = imread(recover.cam1ImgNames[id1]);
		Mat origin2 = imread(recover.cam2ImgNames[id2]);
		Mat mask1 = imread(recover.cam1MaskNames[id1], 0);
		Mat mask2 = imread(recover.cam2MaskNames[id2], 0);

		int idInSFM1 = recover.frame2Cam[make_pair(1, id1)];
		int idInSFM2 = recover.frame2Cam[make_pair(2, id2)];
		
		CameraModel newCM = recover.sfmLoader.allCameras[idInSFM1].getMedian(recover
			.sfmLoader.allCameras[idInSFM2]);

		fgWarper.addWarpedPair(newCM, recover
			.allForeGroundScenePoints[i], origin1, origin2, mask1, mask2);

		//write to img
		string newFg1 = outFolder + string("\\homo1.jpg");
		string newFg2 = outFolder + string("\\homo2.jpg");
		REPORT(newFg1);
		imwrite(newFg1, fgWarper.warpedFgCam1.back());
		imwrite(newFg2, fgWarper.warpedFgCam2.back());
		return;
	}
}

string StitchSolver::join_path(const char* s)
{
	return outFolder + string("\\") + string(s);
}

void StitchSolver::preprocessMask()
{
	recover.getFilePaths(maskFolder, oriFolder);
	
	//cut region
	/*
	for(auto& imgname : recover.cam1MaskNames)
	{
		Mat img = imread(imgname);
		Mat imgROI(img, Rect(0, 520, 1080, 1400));
		imwrite(imgname, imgROI);
	}
	for(auto& imgname : recover.cam2MaskNames)
	{
		Mat img = imread(imgname);
		Mat imgROI(img, Rect(0, 520, 1080, 1400));
		imwrite(imgname, imgROI);
	}
	*/

	//fill holes and dilate to some extent
	int thresh = 100;
	int dilation_type = MORPH_ELLIPSE;
	int dilation_size = 1;
	int filter = 128;

	for(auto &m : recover.cam1MaskNames)
	{
		Mat mask = imread(m, 0);
		medianFilter(mask, filter);
		vector<vector<Point> > contours;
		vector<Vec4i> hierarchy;

		Mat filling_dst = Mat(mask.rows, mask.cols, CV_8U, Scalar(0)); 
		Mat canny_dst, dilation_dst; 

		Canny( mask, canny_dst, thresh, thresh*2, 3 );
		findContours(canny_dst, contours, hierarchy, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, Point(0, 0) );
		fillPoly(filling_dst, contours, Scalar(255));

		Mat element = getStructuringElement(dilation_type, Size( 2*dilation_size + 1, 2*dilation_size+1 ),
		Point( dilation_size, dilation_size ) );
		dilate(filling_dst, dilation_dst, element);

		imwrite(m, dilation_dst);
	}

	for(auto &m : recover.cam2MaskNames)
	{
		Mat mask = imread(m, 0);
		medianFilter(mask, filter);
		vector<vector<Point> > contours;
		vector<Vec4i> hierarchy;

		Mat filling_dst = Mat(mask.rows, mask.cols, CV_8U, Scalar(0)); 
		Mat canny_dst, dilation_dst; 

		Canny( mask, canny_dst, thresh, thresh*2, 3 );
		findContours(canny_dst, contours, hierarchy, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, Point(0, 0) );
		fillPoly(filling_dst, contours, Scalar(255));

		Mat element = getStructuringElement(dilation_type, Size( 2*dilation_size + 1, 2*dilation_size+1 ),
		Point( dilation_size, dilation_size ) );
		dilate(filling_dst, dilation_dst, element);

		imwrite(m, dilation_dst);
	}

}

void StitchSolver::preprocessOrigin()
{
	recover.getFilePaths(maskFolder, oriFolder);

	//cut region
	/*
	for(auto& imgname : recover.cam1ImgNames)
	{
		Mat img = imread(imgname);
		Mat imgROI(img, Rect(0, 520, 1080, 1400));
		imwrite(imgname, imgROI);
	}
	for(auto& imgname : recover.cam2ImgNames)
	{
		Mat img = imread(imgname);
		Mat imgROI(img, Rect(0, 520, 1080, 1400));
		imwrite(imgname, imgROI);
	}
	*/

	//histogram matching
	Mat target = imread(recover.cam1ImgNames[0]);
	Mat target_mask = imread(recover.cam1MaskNames[0], 0);
	if(target.empty() || target_mask.empty())
		return ;

	for(unsigned id = 0; id < recover.cam2ImgNames.size(); id++)
	{
		Mat img = imread(recover.cam2ImgNames[id]);
		Mat mask = imread(recover.cam2MaskNames[id], 0);
		int nrows = img.rows;
		int ncols = img.cols;

	int CI[256][3];
	int CJ[256][3];
	double PI[256][3];
	double PJ[256][3];
	uchar LUT[256][3];
	int numI = 0, numJ = 0;

	for(int i = 0; i < 256; i++)
		for(int k = 0; k < 3; k++)
		{
			CI[i][k] = 0;
			CJ[i][k] = 0;
		}

	for(int y = 0; y < nrows; y++)
		for(int x = 0; x < ncols; x++)
			for(int k = 0; k < 3; k++)
			{
				//if(mask.at<uchar>(y, x) > 127)
				{
					uchar val = img.at<Vec3b>(y, x)[k];
					CI[val][k] += 1;
					numI ++;
				}
			}

	for(int y = 0; y < target.rows; y++)
		for(int x = 0; x < target.cols; x++)
			for(int k = 0; k < 3; k++)
			{
				//if(target_mask.at<uchar>(y, x) > 127)
				{
					uchar val =  target.at<Vec3b>(y, x)[k];
					CJ[val][k] += 1;
					numJ ++;
				}
			}

	for(int i = 0; i < 256; i++)
		for(int k = 0; k < 3; k++)
		{
			if(i > 0)
			{
				CI[i][k] += CI[i-1][k];
				CJ[i][k] += CJ[i-1][k];
			}
		}
	
	for(int i = 0; i < 256; i++)
		for(int k = 0; k < 3; k++)
		{
			PI[i][k] = 1.0 * CI[i][k] / numI;
			PJ[i][k] = 1.0 * CJ[i][k] / numJ;
		}

	for(int k = 0; k < 3; k++)
	{
		uchar j = 0;
		for(int i = 0; i < 256; i++)
		{
			while(PJ[j][k] < PI[i][k] && j < 255)
				j ++;
			LUT[i][k] = j;
		}
	}

	Mat res = Mat(img.size(), CV_8UC3);
	for(int y = 0; y < nrows; y++)
		for(int x = 0; x < ncols; x++)
			for(int k = 0; k < 3; k++)
				res.at<Vec3b>(y, x)[k] = LUT[img.at<Vec3b>(y, x)[k]][k];

	REPORT(recover.cam2ImgNames[id]);
	imwrite(recover.cam2ImgNames[id], res);

	}
}

void StitchSolver::medianFilter(Mat& image, int filter)
{
	for(int x=0; x<image.cols; x++)
		for(int y=0; y<image.rows; y++)
		{
			uchar ptr = image.at<uchar>(y,x);
			if(ptr > filter)//10
				image.at<uchar>(y,x) = 255;
			else
				image.at<uchar>(y,x) = 0;
		}

		int h = image.rows;
		int w = image.cols;
		for(int x=0; x<image.cols; x++)
			for(int y=0; y<image.rows; y++)
			{
				int num = 0;
				int sum = 0;
				for(int i=-1; i<2; i++)
					for(int j=-1; j<2; j++)
						if(x+i>=0 && x+i<w && y+j>=0 && y+j<h)
						{
							uchar ptr = image.at<uchar>(y+j, x+i);
							sum ++;
							if(ptr == 0)
								num ++;
						}
				if(num > 4 || sum < 9)
				{
					image.at<uchar>(y, x) = 0; 
				}
				else
					image.at<uchar>(y, x) = 255;
			}
}

void StitchSolver::warpMLS()
{
	for(int i = 0; i < recover.matchedFramesID.size(); i++)
	{
		Mat warped1 = warpMLS(i, true);
		Mat warped2 = warpMLS(i, false);
		string newFg1 = outFolder + string("\\MLS1.jpg");
		string newFg2 = outFolder + string("\\MLS2.jpg");
		imwrite(newFg1, warped1);
		imwrite(newFg2, warped2);

		cout << "WarpOnMesh Frame: " << i << endl;
		return;
	}
}

Mat StitchSolver::warpMLS(int frameMatchId, bool isSequence1)
{
	int frameId1 = recover.matchedFramesID[frameMatchId].first;
	int frameId2 = recover.matchedFramesID[frameMatchId].second;
	int idInSFM1 = recover.frame2Cam[make_pair(1, frameId1)];
	int idInSFM2 = recover.frame2Cam[make_pair(2, frameId2)];

	Mat origin, mask;
	if(isSequence1)
	{
		origin = imread(recover.cam1ImgNames[frameId1]);
		mask = imread(recover.cam1MaskNames[frameId1], 0);
	}
	else
	{
		origin = imread(recover.cam2ImgNames[frameId2]);
		mask = imread(recover.cam2MaskNames[frameId2], 0);
	}


	vector<scenePointOnPair>& ScnPoints = recover.allForeGroundScenePoints[frameMatchId];
	vector<Point2f> oriPoints, tgtPoints;
	vector<Point3f> scnPoints;
	
	//fill in foreground points
	for(int k = 0; k < 1; k++)
	{
		for(unsigned i = 0; i < ScnPoints.size(); i++)
		{
			if(isSequence1)
			{
				oriPoints.push_back(ScnPoints[i].pos2D_1);
				tgtPoints.push_back(ScnPoints[i].pos2D_2);
			}
			else
			{
				oriPoints.push_back(ScnPoints[i].pos2D_2);
				tgtPoints.push_back(ScnPoints[i].pos2D_1);
			}
			scnPoints.push_back(ScnPoints[i].scenePos);
		}
	}

	//fill in background points
	/*
	vector<scenePoint> BGScnPointsCam1 = recover.allBackGroundPointsCam1[frameId1];
	vector<scenePoint> BGScnPointsCam2 = recover.allBackGroundPointsCam2[frameId2];
	for(unsigned i = 0; i < BGScnPointsCam1.size(); i++)
	{
		if(isSequence1)
		{
			oriPoints.push_back(BGScnPointsCam1[i].pos2D);
			scnPoints.push_back(BGScnPointsCam1[i].scenePos);
		}
		else
			tgtPoints.push_back(BGScnPointsCam1[i].pos2D);
	}
	for(unsigned i = 0; i < BGScnPointsCam2.size(); i++)
	{
		if(!isSequence1)
		{
			oriPoints.push_back(BGScnPointsCam2[i].pos2D);
			scnPoints.push_back(BGScnPointsCam2[i].scenePos);
		}
		else
			tgtPoints.push_back(BGScnPointsCam2[i].pos2D);
	}
	*/
	generator = ViewGenerator(oriPoints, tgtPoints, scnPoints, FrameW, FrameH, GridX, GridY);

	//generator.getNewFeaturesPos(recover.sfmLoader.allCameras[recover.frame2Cam[make_pair(2, frameId2)]]);
	generator.getNewFeaturesPos(recover.sfmLoader.allCameras[idInSFM1].getMedian(recover.sfmLoader.allCameras[idInSFM2]));

	Mat out(FrameH, FrameW, CV_8UC3, Scalar(255, 255, 255));
	generator.warpMLS(origin, mask, out);

	return out;
}

void StitchSolver::extractFeatureFG()
{
	recover.getFilePaths(maskFolder, fgFolder); 
	/*
	for(unsigned i = 0; i < recover.cam1ImgNames.size(); i++)
	{
		Mat img = imread(recover.cam1ImgNames[i]);
		Mat mask = imread(recover.cam1MaskNames[i], 0);
		extractFeatureFG(img, mask, recover.cam1ImgNames[i]);
	}
	*/
	for(unsigned i = 30; i < recover.cam2ImgNames.size(); i++)
	{
		Mat img = imread(recover.cam2ImgNames[i]);
		Mat mask = imread(recover.cam2MaskNames[i], 0);
		extractFeatureFG(img, mask, recover.cam2ImgNames[i]);
	}
}

void StitchSolver::extractFeatureFG(Mat& img, Mat& mask, string imgPath)
{
	string featureFG;
	Configer::getConfiger()->getString("input", "featureFG", featureFG);

	initModule_nonfree(); 
	Ptr<FeatureDetector> detector = FeatureDetector::create( featureFG );
	Ptr<DescriptorExtractor> descriptor_extractor = DescriptorExtractor::create( featureFG );
	if( detector.empty() || descriptor_extractor.empty() )    
		cout << "fail to create detector!";    
		
	//detect feature position
	vector<KeyPoint> kp;    
	detector->detect( img, kp );   
	LOG << "All feature point num: " << (int)kp.size() << '\n';

	//remove feature in background
	unsigned k = 0;
	while(k < kp.size())
	{
		if(mask.at<uchar>(kp[k].pt.y, kp[k].pt.x) < 127)
			kp.erase(kp.begin() + k);
		else
			k ++;
	}
	LOG << "FG feature point num: " << (int)kp.size() << '\n';
		 
	//extract feature vector
	Mat descriptors;    
	descriptor_extractor->compute( img, kp, descriptors );    

	//write feature to file
	string featFile;
	if(featureFG == "SURF")
		featFile = imgPath.substr(0, imgPath.length()-4) + string(".surf");
	REPORT(featFile);

	ofstream fout(featFile);
	fout << descriptors.rows << endl;
	for(int i = 0; i < descriptors.rows; i++)
	{
		fout << kp[i].pt.x << ' '<< kp[i].pt.y << endl;
		for(int j = 0; j < 64; j++)
			fout << descriptors.at<float>(i, j) << endl;
	}
	fout.close();
}

void StitchSolver::blendBGFG()
{
	LOG << "Blending warped foreground and background...\n";

	string fgResFolder = join_path("warpedFG1");
	string bgResFolder = join_path("warpedBG");

	vector<string> warpedFgList, warpedMaskList, warpedBgList, warpedBgLeftList;
	//read file path
	boost::filesystem::recursive_directory_iterator end_iter;
	for (boost::filesystem::recursive_directory_iterator iter(fgResFolder); iter != end_iter; iter++)
	{
		if (!boost::filesystem::is_directory(*iter)){
			string currentImagePath = iter->path().string(); 
#ifdef OLD_BOOST
				string currentImageS = iter->path().filename();
#else
			string currentImageS = iter->path().filename().string();
#endif
			if (iter->path().extension() == string(".jpg") ||
				iter->path().extension() == string(".png")){
				if (currentImageS.find("img") != string::npos)
				{
					warpedFgList.push_back(currentImagePath);
				}
				else if (currentImageS.find("mask") != string::npos)
				{
					warpedMaskList.push_back(currentImagePath);
				}
			}
		}
	}

	for (boost::filesystem::recursive_directory_iterator iter(bgResFolder); iter != end_iter; iter++)
	{
		if (!boost::filesystem::is_directory(*iter)){
			string currentImagePath = iter->path().string();
#ifdef OLD_BOOST
			string currentImageS = iter->path().filename();
#else
			string currentImageS = iter->path().filename().string();
#endif
			if (iter->path().extension() == string(".jpg") ||
				iter->path().extension() == string(".png")){
				if (currentImageS.find("A") != string::npos)
				{
					warpedBgLeftList.push_back(currentImagePath);
				}
				else if (currentImageS.find("B") != string::npos)
				{
					continue;
				}
				else
				{
					warpedBgList.push_back(currentImagePath);
				}
			}
		}
	}

	Point2f anchor_fg, anchor_bg;
	for(unsigned fid = 0; fid < warpedFgList.size(); fid++)
	{
		Mat warpedFg = imread(warpedFgList[fid]);
		Mat warpedMask = imread(warpedMaskList[fid], 0);
		Mat warpedBg = imread(warpedBgList[fid]);
		Mat warpedBgLeft = imread(warpedBgLeftList[fid]);
		Mat stitchingRes = warpedBg.clone();

		if(fid == 0)
		{
			anchor_fg = findAnchor(warpedFg, false);
			anchor_bg = findAnchor(warpedBgLeft, true);
		}
		
		//cout << anchor_fg.x << ' ' << anchor_fg.y ; 
		//cout << anchor_bg.x << ' ' << anchor_bg.y ; 
 
		//crop and copy
		for(int y = 0; y < warpedFg.rows; y++)
		{
			for(int x = 0; x < warpedFg.cols; x++)
			{
				uchar m = warpedMask.at<uchar>(y, x);
				if(5 < m && m < 250)
				{
					int destX = x - anchor_fg.x + anchor_bg.x - 60;
					int destY = y - anchor_fg.y + anchor_bg.y + 20;
					stitchingRes.at<Vec3b>(destY, destX) = warpedFg.at<Vec3b>(y, x);
				}
			}
		}

		char path_res[100];
		sprintf_s(path_res, "%s\\final\\final%d.jpg", outFolder.c_str(), (int)fid);
		imwrite(path_res, stitchingRes);
		LOG << "Stitching " << fid <<"th frame finished\n";
	}

}

Point2f StitchSolver::findAnchor(Mat& img, bool isBlackBorder)
{
	int W = img.cols;
	int H = img.rows;

	Vec3b border;
	if(isBlackBorder)
		border = Vec3b(0,0,0);
	else
		border = Vec3b(255,255,255);

	for(int sum = W+H-2; sum > 0; sum--)
	{
		for(int x = W-1; x > 0; x--)
		{
			int y = sum - x;
			if(img.at<Vec3b>(y, x) != border )
			{
				return Point2f(x, y);
			}
			if(y == H-1)
				break;
		}
	}
}
