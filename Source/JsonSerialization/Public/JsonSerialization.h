// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Modules/ModuleManager.h"

class JSONSERIALIZATION_API FJsonSerializationModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	

	static TSharedPtr<FJsonObject> SerializeUObjectToJson(const UObject* Object, bool bIncludeObjectClasses = false, bool bChangedPropertiesOnly = false);
	static void DeserializeJsonToUObject(UObject*& Object, TSharedPtr<FJsonObject> JsonObject, bool bIncludeObjectClasses = false);
};

struct FJsonSerializerFields {
	static const FName ObjectClassField;
	static const FName ObjectNameField;
	static const FName ObjectPropertiesField;
};

const FName FJsonSerializerFields::ObjectClassField = FName("Class");
const FName FJsonSerializerFields::ObjectNameField = FName("Name");
const FName FJsonSerializerFields::ObjectPropertiesField = FName("Properties");