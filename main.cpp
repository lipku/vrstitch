

#include <stdint.h>
#include <iostream>

#include "CmdArgsMap.hpp"

#include "xml_util/xml_utility_video.h"
#include "xml_util/xml_utility_audio.h"
#include "xml_util/xml_utility_hl.h"

#include "app.h"

static int loadconfig(const std::string& xmlPath, appParams *params,
	std::string& rig_spec_name, std::string& calib_rig_spec_name, std::string& video_input_name, std::string& stitcher_spec_name,  std::string& audio_rig_spec_name, std::string& audio_input_name)
{
	rapidxml::xml_document<> doc;

	// Get Document
	std::string xmFileString;
	if (!xmlutil::getXmlFileString(xmlPath, xmFileString))
	{
		return -1;
	}
	try
	{
		doc.parse<0>((char*)xmFileString.c_str());
	}
	catch (const rapidxml::parse_error& e)
	{
		std::cerr << "Parsing error with xml:" << xmlPath << "\n";
		std::cerr << "Parse error was: " << e.what() << "\n";
		return -1;
	}

	rapidxml::xml_node<>* configNode = doc.first_node("config");
	params->input_dir_base = configNode->first_node("input_dir_base")->value();
	rig_spec_name = configNode->first_node("rig_spec")->value();
	calib_rig_spec_name = configNode->first_node("calib_rig_spec")->value();
	stitcher_spec_name = configNode->first_node("stitcher_spec")->value();
	video_input_name = configNode->first_node("video_input")->value();
	params->rtmp_addr = configNode->first_node("rtmp_addr")->value();

	if (!strcmp(configNode->first_node("use_calibrate")->value(), "true"))
	{
		params->use_calibrate = true;
	}

	rapidxml::xml_node<>* recordNode = configNode->first_node("record");
	if (recordNode != nullptr)
	{
		params->record_flag = true;
		params->record_path = recordNode->first_node("record_path")->value();
	}

	rapidxml::xml_node<>* audioNode = configNode->first_node("audio");
	if (audioNode != nullptr)
	{
		params->audio_flag = true;
		audio_rig_spec_name = audioNode->first_node("audio_rig_spec")->value();
		audio_input_name = audioNode->first_node("audio_input")->value();
		params->audio_type = audioNode->first_node("audio_type")->value();
	}

	return 0;
}


int main(int argc, char *argv[])
{
	app myApp;
	appParams myAppParams = {};

	bool show_help = false;
	std::string rig_spec_name;
	std::string audio_rig_spec_name;
	std::string calib_rig_spec_name("calibrated_rig_spec.xml");
	std::string stitcher_spec_name;
	std::string video_input_name;
	std::string audio_input_name;
	bool out_calib_name_present = false;

	// Process command line arguments
	CmdArgsMap cmdArgs = CmdArgsMap(argc, argv, "--")
		("help", "Produce help message", &show_help);
		/*("rig_spec", "XML file containing rig specification", &rig_spec_name, rig_spec_name)
		("audio_rig_spec", "XML file containing specification of audio sources", &audio_rig_spec_name, audio_rig_spec_name)
		("calib_rig_spec", "Output XML file containing updated rig specification from calibration", &calib_rig_spec_name, calib_rig_spec_name, &out_calib_name_present)
		("stitcher_spec", "XML file containing stitcher specification", &stitcher_spec_name, stitcher_spec_name)
		("video_input", "XML file containing video input specification", &video_input_name, video_input_name)
		("audio_input", "XML file containing audio input specification", &audio_input_name, audio_input_name)
		("input_dir_base", "Base directory for input MP4 files", &myAppParams.input_dir_base, myAppParams.input_dir_base)
		("calib", "Flag to indicate that calibration should be performed", &myAppParams.calib_flag)
		("audio", "Flag to indicate that audio stitching should be performed", &myAppParams.audio_flag)
		("audio_type", "ipcam or mic", &myAppParams.audio_type, myAppParams.audio_type)
		("rtmp_addr", "Rtmp push address", &myAppParams.rtmp_addr, myAppParams.rtmp_addr)
		("record", "Flag to indicate that record should be performed", &myAppParams.record_flag)
		("record_path", "record path", &myAppParams.record_path, myAppParams.record_path);*/

	if (show_help)
	{
		std::cout << "Low-Level Stereo Stitch Sample Application" << std::endl;
		std::cout << cmdArgs.help();
		return 1;
	}

	//myAppParams.calib_flag = true;
	if (loadconfig("config.xml",&myAppParams,rig_spec_name, calib_rig_spec_name, video_input_name, stitcher_spec_name,audio_rig_spec_name, audio_input_name))
	{
		std::cout << "Load config file fail" << std::endl;
		std::cout << cmdArgs.help();
		return 1;
	}

	if (rig_spec_name.empty())
	{
		std::cout << "Rig specification XML name needs to be set using --rig_spec." << std::endl;
		std::cout << cmdArgs.help();
		return 1;
	}

	if (audio_rig_spec_name.empty() && myAppParams.audio_flag)
	{
		std::cout << "If audio is enabled, audio sources specification XML name needs to be set using --audio_rig_spec." << std::endl;
		std::cout << cmdArgs.help();
		return 1;
	}

	if (stitcher_spec_name.empty())
	{
		std::cout << "Stitcher specification XML name needs to be set using --stitcher_spec." << std::endl;
		std::cout << cmdArgs.help();
		return 1;
	}

	if (video_input_name.empty())
	{
		std::cout << "Video input specification XML name needs to be set using --video_input." << std::endl;
		std::cout << cmdArgs.help();
		return 1;
	}

	if (audio_input_name.empty() && myAppParams.audio_flag)
	{
		std::cout << "If audio is enabled, audio input specification XML name needs to be set using --audio_input." << std::endl;
		std::cout << cmdArgs.help();
		return 1;
	}

	if (!out_calib_name_present && myAppParams.calib_flag)
	{
		std::cout << std::endl << "Calibration output XML file not specified, calibrated rig will be saved to " << calib_rig_spec_name << std::endl;
	}

	// Fetch rig parameters from XML file.
	if (!xmlutil::readCameraRigXml(myAppParams.input_dir_base + rig_spec_name, myAppParams.cam_properties, &myAppParams.rig_properties))
	{
		std::cout << std::endl << "Failed to retrieve rig parameters from XML file." << std::endl;
		return 1;
	}
	if (!xmlutil::readCameraRigXml(myAppParams.input_dir_base + calib_rig_spec_name, myAppParams.calibrated_cam_properties, &myAppParams.calibrated_rig_properties))
	{
		std::cout << std::endl << "Failed to retrieve rig parameters from XML file." << std::endl;
		return 1;
	}

	// Fetch image filenames for calibration
	if (myAppParams.calib_flag)
	{
		if (!xmlutil::readInputCalibFilenamesXml(myAppParams.input_dir_base + rig_spec_name, myAppParams.calib_filenames))
		{
			std::cout << "XML Error reading calibration filenames." << std::endl;
			return 1;
		}

		// Calibrate
		if (myApp.calibrate(&myAppParams) != NVSTITCH_SUCCESS)
		{
			std::cout << "Calibration failure." << std::endl;
			return 1;
		}

		// Save updated rig properties resulting from calibration
		if (!xmlutil::writeCameraRigXml(myAppParams.input_dir_base + calib_rig_spec_name, &myAppParams.calibrated_rig_properties))
		{
			std::cout << "XML Error writing updated rig parameters resulting from calibration." << std::endl;
			return 1;
		}
	}

	// Fetch input video feeds from XML file.
	std::vector<std::string> payloadFilenames;
	payloadFilenames.reserve(myAppParams.cam_properties.size());
	if (!xmlutil::readInputMediaFeedXml(myAppParams.input_dir_base + video_input_name, myAppParams.payloads, payloadFilenames))
	{
		std::cout << std::endl << "Failed to retrieve input video feeds from XML file." << std::endl;
		return 1;
	}

	std::string audio_input;
	if (myAppParams.audio_flag)
	{
		audio_input = myAppParams.input_dir_base + audio_input_name;
	}

	// Fetch stitcher parameters from XML file.
	std::vector<std::string> outputPayloadFilenames;
	outputPayloadFilenames.reserve(myAppParams.cam_properties.size());
	if (!xmlutil::readStitcherPropertiesXml(myAppParams.input_dir_base + stitcher_spec_name,
											&myAppParams.stitcher_properties,
											outputPayloadFilenames,	
											audio_input))
	{
		std::cout << std::endl << "Failed to retrieve stitcher parameters from XML file." << std::endl;
		return 1;
	}

	if (myAppParams.audio_flag)
	{
		// Fetch audio rig properties from XML file.
		if (!xmlutil::readAudioRigXml(myAppParams.input_dir_base + audio_rig_spec_name, &myAppParams.audio_rig_properties))
		{
			std::cout << std::endl << "Failed to retrieve audio rig parameters from XML file." << std::endl;
			return 1;
		}

		// Fetch input audio feeds from XML file.
		std::vector<std::string> audioPayloadFilenames;
		audioPayloadFilenames.reserve(myAppParams.cam_properties.size());
		if (!xmlutil::readInputAudioFeedXml(myAppParams.input_dir_base + audio_input_name, myAppParams.audio_payloads, audioPayloadFilenames))
		{
			std::cout << std::endl << "Failed to retrieve input audio feeds from XML file." << std::endl;
			return 1;
		}
	}

	// Stitch
	if (myApp.run(&myAppParams) != NVSTITCH_SUCCESS)
	{
		std::cout << "Stitching failed." << std::endl;
		return 1;
	}

	return 0;
}