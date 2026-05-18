#include "HdemRecorder.hpp"

#include "Entity.hpp"
#include "Features/EntityList.hpp"
#include "Modules/Engine.hpp"
#include "Modules/Server.hpp"
#include "Version.hpp"

#include <cstring>
#include <ctime>

HdemRecorder::~HdemRecorder() {
	Stop();
}

uint16_t HdemRecorder::GetOrAddClass(const std::string &className) {
	auto it = classNameToId.find(className);
	if (it != classNameToId.end()) return it->second;
	uint16_t id = static_cast<uint16_t>(classes.size());
	classNameToId[className] = id;
	classes.push_back({id, className, {}});
	return id;
}

uint16_t HdemRecorder::GetOrAddField(const std::string &fieldName, HdemFieldType type) {
	auto it = fieldNameToId.find(fieldName);
	if (it != fieldNameToId.end()) return it->second;
	uint16_t id = static_cast<uint16_t>(allFields.size());
	fieldNameToId[fieldName] = id;
	allFields.push_back({id, fieldName, type});
	return id;
}

void HdemRecorder::DiscoverEntities() {
	if (schemaDiscovered) return;

	classes.clear();
	allFields.clear();
	classNameToId.clear();
	fieldNameToId.clear();

	// Universal fields definition (deterministic IDs matching HdemWellKnownField enum)
	uint16_t originId = GetOrAddField("m_vecAbsOrigin", HDEM_VEC3);
	uint16_t anglesId = GetOrAddField("m_angAbsRotation", HDEM_VEC3);
	uint16_t velocityId = GetOrAddField("m_vecAbsVelocity", HDEM_VEC3);

	// Specialized prop_portal properties
	uint16_t activatedId = GetOrAddField("m_bActivated", HDEM_BOOL);
	uint16_t isPortal2Id = GetOrAddField("m_bIsPortal2", HDEM_BOOL);
	uint16_t linkedPortalId = GetOrAddField("m_hLinkedPortal", HDEM_HANDLE);

	// New universal/specialized properties for cubes / doors / buttons / turrets etc.
	uint16_t healthId = GetOrAddField("m_iHealth", HDEM_INT32);
	uint16_t flagsId = GetOrAddField("m_fFlags", HDEM_INT32);
	uint16_t ownerEntityId = GetOrAddField("m_hOwnerEntity", HDEM_HANDLE);
	uint16_t lockedId = GetOrAddField("m_bLocked", HDEM_BOOL);
	uint16_t toggleStateId = GetOrAddField("m_toggle_state", HDEM_INT32);
	uint16_t classnameId = GetOrAddField("m_iClassname", HDEM_STRING);
	uint16_t nameId = GetOrAddField("m_iName", HDEM_STRING);

	// Pre-register critical classes guaranteed to be needed during gameplay rollouts,
	// ensuring their metadata schemas are embedded in the header regardless of spawn timing.
	auto addClassSchema = [&](const std::string &cname) {
		uint16_t cid = GetOrAddClass(cname);
		auto &cls = classes[cid];
		if (cls.fields.empty()) {
			cls.fields.push_back(allFields[originId]);
			cls.fields.push_back(allFields[anglesId]);
			cls.fields.push_back(allFields[velocityId]);
			cls.fields.push_back(allFields[healthId]);
			cls.fields.push_back(allFields[flagsId]);
			cls.fields.push_back(allFields[ownerEntityId]);

			if (cname == "prop_portal") {
				cls.fields.push_back(allFields[activatedId]);
				cls.fields.push_back(allFields[isPortal2Id]);
				cls.fields.push_back(allFields[linkedPortalId]);
			} else if (cname == "prop_button") {
				cls.fields.push_back(allFields[lockedId]);
			} else if (cname == "func_weight_button") {
				cls.fields.push_back(allFields[toggleStateId]);
			} else if (cname == "prop_testchamber_door") {
				cls.fields.push_back(allFields[toggleStateId]);
				cls.fields.push_back(allFields[lockedId]);
			}
		}
	};

	addClassSchema("prop_portal");
	addClassSchema("player");
	addClassSchema("portal_player");
	addClassSchema("prop_physics");
	addClassSchema("prop_dynamic");
	addClassSchema("trigger_portal_cleanser");
	addClassSchema("prop_weighted_cube");
	addClassSchema("prop_button");
	addClassSchema("func_weight_button");
	addClassSchema("prop_testchamber_door");

	if (!server || !entityList) return;

	// Scan any additional entities present in the world at recording startup
	for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
		auto info = entityList->GetEntityInfoByIndex(i);
		if (!info || !info->m_pEntity) continue;

		auto ent = info->m_pEntity;
		const char *className = server->GetEntityClassName(ent);
		if (!className) continue;

		addClassSchema(className);
	}

	schemaDiscovered = true;
}

template <typename T>
static inline void WritePOD(std::ofstream &out, const T &val) {
	out.write(reinterpret_cast<const char *>(&val), sizeof(T));
}

static inline void WriteString(std::ofstream &out, const std::string &str) {
	out.write(str.c_str(), str.size() + 1);
}

void HdemRecorder::WriteHeader(const std::string &mapName, float tickrate) {
	WritePOD(file, HDEM_MAGIC);
	WritePOD(file, HDEM_VERSION);
	WritePOD(file, uint16_t(0));  // Flags

	WriteString(file, mapName);
	WritePOD(file, tickrate);
	WritePOD(file, static_cast<uint64_t>(time(nullptr)));
	WriteString(file, SAR_VERSION);
	WriteString(file, engine ? engine->GetGameDirectory() : "");

	// Write placeholder for trailing schema offset
	schemaOffsetPos = file.tellp();
	WritePOD(file, uint64_t(0));

	headerWritten = true;
	totalBytes = file.tellp();
}

void HdemRecorder::WriteClassTable() {
	WritePOD(file, static_cast<uint16_t>(classes.size()));
	for (const auto &cls : classes) {
		WritePOD(file, cls.classId);
		WriteString(file, cls.name);
	}
}

void HdemRecorder::WriteFieldTable() {
	WritePOD(file, static_cast<uint16_t>(allFields.size()));
	for (const auto &field : allFields) {
		WritePOD(file, field.fieldId);
		WriteString(file, field.name);
		WritePOD(file, static_cast<uint8_t>(field.type));
	}
}

bool HdemRecorder::Start(const std::string &path, const std::string &mapName, float tickrate) {
	if (isActive) Stop();

	file.open(path, std::ios::binary | std::ios::out);
	if (!file.is_open()) return false;

	isActive = true;
	headerWritten = false;
	totalBytes = 0;
	totalTicks = 0;

	lastEntityState.clear();
	lastEntitySerial.clear();

	DiscoverEntities();
	WriteHeader(mapName, tickrate);
	return true;
}

void HdemRecorder::Stop() {
	if (!isActive) return;

	// Append trailing schema tables
	uint64_t actualSchemaOffset = static_cast<uint64_t>(file.tellp());
	WriteClassTable();
	WriteFieldTable();

	// Write footer
	WritePOD(file, static_cast<uint32_t>(totalTicks));
	WritePOD(file, static_cast<uint32_t>(lastEntitySerial.size()));
	WritePOD(file, uint32_t(0));  // Checksum

	// Backfill real schema offset into header
	file.seekp(schemaOffsetPos);
	WritePOD(file, actualSchemaOffset);

	file.close();
	isActive = false;
	headerWritten = false;
}

template <typename T>
static inline void AppendToBuffer(std::vector<uint8_t> &buf, const T &val) {
	const uint8_t *ptr = reinterpret_cast<const uint8_t *>(&val);
	buf.insert(buf.end(), ptr, ptr + sizeof(T));
}

void HdemRecorder::RecordTick(int tickNumber) {
	if (!isActive || !headerWritten || !server || !entityList) return;

	tickBuffer.clear();
	uint16_t numEntitiesWritten = 0;

	for (int i = 0; i < Offsets::NUM_ENT_ENTRIES; ++i) {
		auto info = entityList->GetEntityInfoByIndex(i);
		if (!info || !info->m_pEntity) continue;

		auto ent = info->m_pEntity;
		const char *classNameCStr = server->GetEntityClassName(ent);
		if (!classNameCStr) continue;

		auto itClass = classNameToId.find(classNameCStr);
		uint16_t classId = 0;
		if (itClass == classNameToId.end()) {
			// Dynamically register new class discovered mid-rollout!
			classId = GetOrAddClass(classNameCStr);
			auto &clsInit = classes[classId];
			if (clsInit.fields.empty()) {
				clsInit.fields.push_back(allFields[HDEM_FIELD_ORIGIN]);
				clsInit.fields.push_back(allFields[HDEM_FIELD_ANGLES]);
				clsInit.fields.push_back(allFields[HDEM_FIELD_VELOCITY]);
				clsInit.fields.push_back(allFields[HDEM_FIELD_HEALTH]);
				clsInit.fields.push_back(allFields[HDEM_FIELD_FLAGS]);
				clsInit.fields.push_back(allFields[HDEM_FIELD_OWNERENTITY]);
				if (std::strcmp(classNameCStr, "prop_portal") == 0) {
					clsInit.fields.push_back(allFields[HDEM_FIELD_ACTIVATED]);
					clsInit.fields.push_back(allFields[HDEM_FIELD_ISPORTAL2]);
					clsInit.fields.push_back(allFields[HDEM_FIELD_LINKEDPORTAL]);
				} else if (std::strcmp(classNameCStr, "prop_button") == 0) {
					clsInit.fields.push_back(allFields[HDEM_FIELD_LOCKED]);
				} else if (std::strcmp(classNameCStr, "func_weight_button") == 0) {
					clsInit.fields.push_back(allFields[HDEM_FIELD_TOGGLESTATE]);
				} else if (std::strcmp(classNameCStr, "prop_testchamber_door") == 0) {
					clsInit.fields.push_back(allFields[HDEM_FIELD_TOGGLESTATE]);
					clsInit.fields.push_back(allFields[HDEM_FIELD_LOCKED]);
				}
			}
		} else {
			classId = itClass->second;
		}
		const auto &cls = classes[classId];

		static std::vector<uint8_t> currentRawState;
		currentRawState.clear();

		ServerEnt *se = SE(ent);

		for (const auto &field : cls.fields) {
			switch (field.fieldId) {
			case HDEM_FIELD_ORIGIN:
				AppendToBuffer(currentRawState, se->abs_origin());
				break;
			case HDEM_FIELD_ANGLES:
				AppendToBuffer(currentRawState, se->abs_angles());
				break;
			case HDEM_FIELD_VELOCITY:
				AppendToBuffer(currentRawState, se->abs_velocity());
				break;
			case HDEM_FIELD_ACTIVATED:
				try {
					AppendToBuffer(currentRawState, se->field<bool>("m_bActivated"));
				} catch (...) {
					AppendToBuffer(currentRawState, bool(false));
				}
				break;
			case HDEM_FIELD_ISPORTAL2:
				try {
					AppendToBuffer(currentRawState, se->field<bool>("m_bIsPortal2"));
				} catch (...) {
					AppendToBuffer(currentRawState, bool(false));
				}
				break;
			case HDEM_FIELD_LINKEDPORTAL:
				try {
					CBaseHandle h = se->field<CBaseHandle>("m_hLinkedPortal");
					AppendToBuffer(currentRawState, static_cast<uint32_t>(h.m_Index));
				} catch (...) {
					AppendToBuffer(currentRawState, uint32_t(0xFFFFFFFF));
				}
				break;
			case HDEM_FIELD_HEALTH:
				try {
					AppendToBuffer(currentRawState, se->field<int>("m_iHealth"));
				} catch (...) {
					AppendToBuffer(currentRawState, int(0));
				}
				break;
			case HDEM_FIELD_FLAGS:
				try {
					AppendToBuffer(currentRawState, se->field<int>("m_fFlags"));
				} catch (...) {
					AppendToBuffer(currentRawState, int(0));
				}
				break;
			case HDEM_FIELD_OWNERENTITY:
				try {
					CBaseHandle h = se->field<CBaseHandle>("m_hOwnerEntity");
					AppendToBuffer(currentRawState, static_cast<uint32_t>(h.m_Index));
				} catch (...) {
					AppendToBuffer(currentRawState, uint32_t(0xFFFFFFFF));
				}
				break;
			case HDEM_FIELD_LOCKED:
				try {
					AppendToBuffer(currentRawState, se->field<bool>("m_bLocked"));
				} catch (...) {
					AppendToBuffer(currentRawState, bool(false));
				}
				break;
			case HDEM_FIELD_TOGGLESTATE:
				try {
					AppendToBuffer(currentRawState, se->field<int>("m_toggle_state"));
				} catch (...) {
					AppendToBuffer(currentRawState, int(0));
				}
				break;
			default:
				break;
			}
		}

		uint16_t serialNum = static_cast<uint16_t>(info->m_SerialNumber);
		bool isNewOrModified = false;
		bool isFullSnapshot = false;

		auto itLastState = lastEntityState.find(i);
		auto itLastSerial = lastEntitySerial.find(i);

		if (itLastState == lastEntityState.end() || itLastSerial == lastEntitySerial.end() || itLastSerial->second != serialNum) {
			isNewOrModified = true;
			isFullSnapshot = true;
		} else {
			if (currentRawState != itLastState->second) {
				isNewOrModified = true;
			}
		}

		if (!isNewOrModified) continue;

		AppendToBuffer(tickBuffer, static_cast<uint16_t>(i));
		AppendToBuffer(tickBuffer, serialNum);
		AppendToBuffer(tickBuffer, classId);

		uint8_t flags = HDEM_ENT_ALIVE;
		if (isFullSnapshot) flags |= HDEM_ENT_FULL_SNAPSHOT;
		AppendToBuffer(tickBuffer, flags);

		size_t numFieldsOffset = tickBuffer.size();
		AppendToBuffer(tickBuffer, uint8_t(0));

		uint8_t fieldsWritten = 0;
		size_t byteOffset = 0;

		for (const auto &field : cls.fields) {
			size_t fsize = HdemFieldSize(field.type);
			bool writeField = isFullSnapshot;

			if (!isFullSnapshot) {
				if (byteOffset + fsize <= itLastState->second.size() && byteOffset + fsize <= currentRawState.size()) {
					if (std::memcmp(&currentRawState[byteOffset], &itLastState->second[byteOffset], fsize) != 0) {
						writeField = true;
					}
				} else {
					writeField = true;
				}
			}

			if (writeField && byteOffset + fsize <= currentRawState.size()) {
				AppendToBuffer(tickBuffer, field.fieldId);
				tickBuffer.insert(tickBuffer.end(), &currentRawState[byteOffset], &currentRawState[byteOffset] + fsize);
				fieldsWritten++;
			}

			byteOffset += fsize;
		}

		if (isFullSnapshot) {
			// Write classname
			AppendToBuffer(tickBuffer, static_cast<uint16_t>(HDEM_FIELD_CLASSNAME));
			const char *cname = server->GetEntityClassName(ent);
			if (!cname) cname = "";
			tickBuffer.insert(tickBuffer.end(), cname, cname + std::strlen(cname) + 1);
			fieldsWritten++;

			// Write targetname
			AppendToBuffer(tickBuffer, static_cast<uint16_t>(HDEM_FIELD_NAME));
			const char *tname = server->GetEntityName(ent);
			if (!tname) tname = "";
			tickBuffer.insert(tickBuffer.end(), tname, tname + std::strlen(tname) + 1);
			fieldsWritten++;
		}

		tickBuffer[numFieldsOffset] = fieldsWritten;

		lastEntityState[i] = currentRawState;
		lastEntitySerial[i] = serialNum;
		numEntitiesWritten++;
	}

	uint32_t frameByteSize = static_cast<uint32_t>(tickBuffer.size());
	WritePOD(file, static_cast<int32_t>(tickNumber));
	WritePOD(file, numEntitiesWritten);
	WritePOD(file, frameByteSize);

	if (frameByteSize > 0) {
		file.write(reinterpret_cast<const char *>(tickBuffer.data()), frameByteSize);
	}

	totalTicks++;
	totalBytes += (sizeof(int32_t) + sizeof(uint16_t) + sizeof(uint32_t) + frameByteSize);
}
