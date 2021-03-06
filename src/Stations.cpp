#include "Stations.hpp"

#include <cstdint>

#include <algorithm>
#include <fstream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <ShlObj.h>

#include <json/json.h>

#include "CryptUtil.hpp"
#include "HTTP.hpp"
#include "StringUtil.hpp"
#include "VersionUtil.hpp"

#include "HTMLFixMetaSource.hpp"
#include "HTTPMetaSource.hpp"
#include "MetaMetaSource.hpp"
#include "RegExMetaSource.hpp"

using std::find_if;
using std::ifstream;
using std::ios;
using std::list;
using std::map;
using std::ofstream;
using std::pair;
using std::string;
using std::stringstream;
using std::vector;
using Json::nullValue;
using Json::Reader;
using Json::Value;

namespace inetr {
	Stations::Stations() {
		MetaSourcePrototypes.push_back(new MetaMetaSource());
		MetaSourcePrototypes.push_back(new HTTPMetaSource());
		MetaSourcePrototypes.push_back(new RegExMetaSource());
		MetaSourcePrototypes.push_back(new HTMLFixMetaSource());
	}

	Stations::~Stations() {
		for (list<MetaSourcePrototype*>::iterator it =
			MetaSourcePrototypes.begin(); it != MetaSourcePrototypes.end();
			++it) {

			delete *it;
		}
	}

	bool Stations::Load() {
		char appDataPath[MAX_PATH];
		SHGetFolderPath(nullptr, CSIDL_COMMON_APPDATA, nullptr,
			SHGFP_TYPE_CURRENT, appDataPath);

		string path = string(appDataPath) + "\\InternetRadio\\stations.json";

		stringstream ssArchive;
		try {
			HTTP::Get(
				"http://internetradio.clemensboos.net/stations/archive.json",
				&ssArchive);
		} catch(...) { }

		Reader jsonReader;
		Value archiveRootValue;

		jsonReader.parse(ssArchive, archiveRootValue);

		map<int, string> staVersions;
		for (size_t i = 0; i < (size_t)archiveRootValue.size(); ++i) {
			string versionStr = archiveRootValue.getMemberNames().at(i);

			Value versionMinVerVal = archiveRootValue.get(versionStr, Value(
				nullValue));
			if (!versionMinVerVal.isString())
				continue;

			staVersions.insert(pair<int, string>(
				atoi(versionStr.c_str()), versionMinVerVal.asString()));
		}

		uint16_t installedVersion[4];
		VersionUtil::GetInstalledVersion(installedVersion);

		for (map<int, string>::reverse_iterator it =
			staVersions.rbegin(); it != staVersions.rend(); ++it) {

			uint16_t staVer[4];
			VersionUtil::VersionStrToArr(it->second, staVer);

			if (VersionUtil::CompareVersions(installedVersion, staVer) !=
				VCR_Older) {

				stringstream ssVer;
				ssVer << it->first;
				stringstream ssNewStaChecksumsF;
				try {
					HTTP::Get("http://internetradio.clemensboos.net/stations/" +
						ssVer.str() + "/checksums", &ssNewStaChecksumsF);
				} catch(...) {
					break;
				}

				while (ssNewStaChecksumsF.good()) {
					string filePathAndChecksum;
					ssNewStaChecksumsF >> filePathAndChecksum;
					if (filePathAndChecksum == "")
						continue;

					vector<string> filePathAndChecksumSplit =
						StringUtil::Explode(filePathAndChecksum, ":");
					if (filePathAndChecksumSplit.size() != 2)
						continue;

					string fileName = filePathAndChecksumSplit[0];
					string locPath = string(appDataPath) + "\\InternetRadio\\"
						+ fileName;
					StringUtil::SearchAndReplace(locPath, "/", "\\");
					string remoteChecksum = filePathAndChecksumSplit[1];

					bool update = false;

					ifstream fileStream(locPath);
					if (fileStream.is_open()) {
						fileStream.close();
						string localChecksum = CryptUtil::FileMD5Hash(locPath);
						if (remoteChecksum != localChecksum)
							update = true;
					} else {
						update = true;
					}

					if (!update)
						continue;

					string fileRemURL =
						"http://internetradio.clemensboos.net/stations/" +
						ssVer.str() + "/" + fileName;
					StringUtil::SearchAndReplace(fileRemURL, "\\", "/");
					stringstream ssRemFile;
					try {
						HTTP::Get(fileRemURL, &ssRemFile);
					} catch(...) {
						continue;
					}

					string locDir = locPath.substr(0,
						locPath.find_last_of("\\"));
					CreateDirectory(locDir.c_str(), nullptr);

					ofstream fOutStream;
					fOutStream.open(locPath, ios::out | ios::binary);
					fOutStream << ssRemFile.rdbuf();
					fOutStream.close();
				}

				break;
			}
		}

		ifstream stationsFile;
		stationsFile.open(path, ios::in);
		if (!stationsFile.is_open()) {
			return false;
		}

		Value rootValue;

		if (!jsonReader.parse(stationsFile, rootValue, false)) {
			MessageBox(nullptr, jsonReader.getFormattedErrorMessages().c_str(),
				"Error", MB_OK | MB_ICONERROR);
			return false;
		}

		stationsFile.close();

		Value minVersionValue = rootValue.get("minVersion", Value(nullValue));
		if (!minVersionValue.isString()) {
			MessageBox(nullptr, "Unable to load stations, couldn't read \
				minimum version", "Error", MB_OK | MB_ICONERROR);
			return false;
		}

		uint16_t minVersion[4];
		VersionUtil::VersionStrToArr(minVersionValue.asString(), minVersion);
		if (VersionUtil::CompareVersions(minVersion, installedVersion) ==
			VCR_Newer) {

			MessageBox(nullptr, "Stations file incompatible, please update \
				this application", "Error", MB_OK | MB_ICONERROR);
			return false;
		}

		Value stationsValue = rootValue.get("stations", Value(nullValue));
		if (!stationsValue.isObject()) {
			MessageBox(nullptr, "Unable to load stations", "Error", MB_OK |
				MB_ICONERROR);
			return false;
		}

		for (size_t i = 0; i < (size_t)stationsValue.size(); ++i) {
			string staIdentifier = stationsValue.getMemberNames().at(i);

			Value staValue = stationsValue.get(staIdentifier, Value(nullValue));
			if (!staValue.isObject()) {
				MessageBox(nullptr, ("Unable to load station: " +
					staIdentifier).c_str(), "Error", MB_OK | MB_ICONERROR);
				continue;
			}

			Value staNameValue = staValue.get("name", Value(nullValue));
			if (!staNameValue.isString()) {
				MessageBox(nullptr, ("Unable to load station: " +
					staIdentifier + "Couldn't read name").c_str(), "Error",
					MB_OK | MB_ICONERROR);
				continue;
			}

			Value staStreamURLValue = staValue.get("streamURL", Value(
				nullValue));
			if (!staStreamURLValue.isString()) {
				MessageBox(nullptr, ("Unable to load station: " +
					staIdentifier + "Couldn't read stream URL").c_str(),
					"Error", MB_OK | MB_ICONERROR);
				continue;
			}

			Value staImageValue = staValue.get("image", Value(nullValue));
			if (!staImageValue.isString()) {
				MessageBox(nullptr, ("Unable to load station: " +
					staIdentifier + "Couldn't read image").c_str(), "Error",
					MB_OK | MB_ICONERROR);
				continue;
			}

			vector<MetaSource> metaSources;
			string metaOut = "";

			Value staMetaValue = staValue.get("meta", Value(nullValue));
			if (staMetaValue.isObject()) {
				Value staMetaSrcsValue = staMetaValue.get("sources",
					Value(nullValue));
				if (!staMetaSrcsValue.isArray()) {
					MessageBox(nullptr, ("Unable to load station: " +
						staIdentifier + "Couldn't read meta sources").c_str(),
						"Error", MB_OK | MB_ICONERROR);
					continue;
				}

				for (unsigned int i = 0; i < staMetaSrcsValue.size(); ++i) {
					Value staMetaSrcVal = staMetaSrcsValue[i];
					if (!staMetaSrcVal.isObject()) {
						MessageBox(nullptr, ("Unable to load station: " +
							staIdentifier +
							"Couldn't read meta source").c_str(), "Error", MB_OK
							| MB_ICONERROR);
						continue;
					}

					Value staMetaSrcIdVal = staMetaSrcVal.get("id", Value(
						nullValue));
					if (!staMetaSrcIdVal.isString()) {
						MessageBox(nullptr, ("Unable to load station: " +
							staIdentifier + "Couldn't read meta source \
							id").c_str(), "Error", MB_OK | MB_ICONERROR);
						continue;
					}
					string staMetaSrcId = staMetaSrcIdVal.asString();

					list<MetaSourcePrototype*>::iterator srcProtIt =
						find_if(MetaSourcePrototypes.begin(),
						MetaSourcePrototypes.end(), [&staMetaSrcId](
						MetaSourcePrototype* &element) -> bool {

						return element->GetIdentifer() == staMetaSrcId;
					});

					if (srcProtIt == MetaSourcePrototypes.end()) {
						MessageBox(nullptr, ("Unable to load station: " +
							staIdentifier + "\nUndefined meta source: " +
							staMetaSrcId).c_str(), "Error", MB_OK |
							MB_ICONERROR);
						continue;
					}

					map<string, string> staMetaSrcParam;
					for (size_t j = 0; j < (size_t)staMetaSrcVal.size(); ++j) {
						string staMetaSrcKey =
							staMetaSrcVal.getMemberNames().at(j);

						if (staMetaSrcKey == "id")
							continue;

						Value staMetaSrcValVal =
							staMetaSrcVal.get(staMetaSrcKey, Value(nullValue));
						if (!staMetaSrcValVal.isString()) {
							MessageBox(nullptr, ("Unable to load station: " +
								staIdentifier + "\nCouldn't read parameter: " +
								staMetaSrcKey + " of meta source: " +
								staMetaSrcId).c_str(), "Error", MB_OK |
								MB_ICONERROR);
							continue;
						}

						staMetaSrcParam.insert(pair<string, string>(
							staMetaSrcKey, staMetaSrcValVal.asString()));
					}

					metaSources.push_back(MetaSource(*srcProtIt, move(
						staMetaSrcParam)));
				}

				Value staMetaOutValue = staMetaValue.get("out",
					Value(nullValue));
				if (!staMetaOutValue.isString()) {
					MessageBox(nullptr, ("Unable to load station: " +
						staIdentifier + "Couldn't read meta output").c_str(),
						"Error", MB_OK | MB_ICONERROR);
					continue;
				}
				metaOut = staMetaOutValue.asString();
			}

			stations.push_back(Station(staIdentifier, staNameValue.asString(),
				staStreamURLValue.asString(), "img/" + staImageValue.asString(),
				std::move(metaSources), metaOut));
		}

		return true;
	}
}
