// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonSerialization.h"

#include "JsonObjectConverter.h"
#include "UObject/UnrealType.h"
#include "InstancedStruct.h"

#define LOCTEXT_NAMESPACE "FJsonSerializationModule"

void FJsonSerializationModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FJsonSerializationModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

struct FPropertyTest {
	FPropertyTest(FProperty* Property) {
		Raw = Property;
		AsArray = CastField< FArrayProperty>(Property);
		AsSet = CastField< FSetProperty>(Property);
		AsMap = CastField< FMapProperty>(Property);
		AsStruct = CastField< FStructProperty>(Property);
		AsObject = CastField< FObjectProperty>(Property);
	};

	FProperty* Raw;
	FArrayProperty* AsArray;
	FSetProperty* AsSet;
	FMapProperty* AsMap;
	FStructProperty* AsStruct;
	FObjectProperty* AsObject;
};

static void SerializePropertyAsJsonObjectField(const void* Data, const UObject* Outer, TSharedPtr<FJsonObject> OuterObject, FProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly);
static void SerializeStructPropertyAsJsonObjectField(const void* InnerPropData, const UObject* Outer, FStructProperty* StructProperty, TSharedPtr<FJsonObject> StructObject, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly);
static TArray<TSharedPtr<FJsonValue>> SerializeArrayPropertyAsJsonArray(const void* Data, const UObject* Outer, FArrayProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly);
static TArray<TSharedPtr<FJsonValue>> SerializeSetPropertyAsJsonArray(const void* Data, const UObject* Outer, FSetProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly);
static TArray<TSharedPtr<FJsonValue>> SerializeMapPropertyAsJsonArray(const void* Data, const UObject* Outer, FMapProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly);

static void SerializeStructPropertyAsJsonObjectField(const void* InnerPropData, const UObject* Outer, FStructProperty* StructProperty, TSharedPtr<FJsonObject> StructObject, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly)
{
	const uint8* StructPropData = StructProperty->ContainerPtrToValuePtr<uint8>(InnerPropData);
	if (StructProperty->Struct == TBaseStructure<FInstancedStruct>::Get())
	{
		const FInstancedStruct& InstancedStruct = *(FInstancedStruct*)StructPropData;
		for (TFieldIterator<FProperty> PropertyItr(InstancedStruct.GetScriptStruct()); PropertyItr; ++PropertyItr)
		{
			SerializePropertyAsJsonObjectField(InstancedStruct.GetMemory(), Outer, StructObject, *PropertyItr, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
		}
	}
	else
	{
		for (TFieldIterator<FProperty> PropertyItr(StructProperty->Struct); PropertyItr; ++PropertyItr)
		{
			SerializePropertyAsJsonObjectField((void*)StructPropData, Outer, StructObject, *PropertyItr, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
		}
	}
}

static TArray<TSharedPtr<FJsonValue>> SerializeArrayPropertyAsJsonArray(const void* Data, const UObject* Outer, FArrayProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly)
{
	const uint8* PropData = Property->ContainerPtrToValuePtr<uint8>(Data);
	FScriptArrayHelper Helper(Property, PropData);
	TArray<TSharedPtr<FJsonValue>> ValueArray;

	FPropertyTest TestProp = FPropertyTest(Property->Inner);

	for (int32 i = 0, n = Helper.Num(); i < n; ++i)
	{
		const uint8* InnerPropData = Helper.GetRawPtr(i);
		if (TestProp.AsArray) // Array
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeArrayPropertyAsJsonArray(InnerPropData, Outer, TestProp.AsArray, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			ValueArray.Emplace(new FJsonValueArray(InnerArray));
		}
		if (TestProp.AsSet) // Set
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeSetPropertyAsJsonArray(InnerPropData, Outer, TestProp.AsSet, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			ValueArray.Emplace(new FJsonValueArray(InnerArray));
		}
		if (TestProp.AsMap) // Map
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeMapPropertyAsJsonArray(InnerPropData, Outer, TestProp.AsMap, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			ValueArray.Emplace(new FJsonValueArray(InnerArray));
		}
		else if (TestProp.AsStruct) // Struct
		{
			TSharedPtr<FJsonObject> StructObject = MakeShareable(new FJsonObject);
			SerializeStructPropertyAsJsonObjectField(InnerPropData, Outer, TestProp.AsStruct, StructObject, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			ValueArray.Emplace(new FJsonValueObject(StructObject));
		}
		else if (TestProp.AsObject) // Object
		{
			const UObject* SubObject = TestProp.AsObject->GetObjectPropertyValue_InContainer(InnerPropData);
			if (SubObject->IsValidLowLevel() && SubObject->GetOuter() == Outer && !TraversedObjects.Contains(SubObject))
			{
				TraversedObjects.Add(SubObject);
				TSharedPtr<FJsonObject> JsonSubObject = MakeShared<FJsonObject>();
				TSharedPtr<FJsonObject> JsonSubObjectProperties = JsonSubObject;

				if (bIncludeObjectClasses) {
					JsonSubObjectProperties = MakeShared<FJsonObject>();
					JsonSubObject->SetStringField(FJsonSerializerFields::ObjectClassField.ToString(), SubObject->GetClass()->GetPathName());
					JsonSubObject->SetObjectField(FJsonSerializerFields::ObjectPropertiesField.ToString(), JsonSubObjectProperties);
				}

				for (TFieldIterator<FProperty> PropertyItr(SubObject->GetClass()); PropertyItr; ++PropertyItr)
				{
					SerializePropertyAsJsonObjectField(SubObject, SubObject, JsonSubObjectProperties, *PropertyItr, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
				}
				ValueArray.Emplace(new FJsonValueObject(JsonSubObject));
			}
			else {
				ValueArray.Emplace(new FJsonValueString(SubObject->GetPathName()));
			}
		}
		else
		{
			TSharedPtr<FJsonValue> JsonValue;
			const uint8* InnerInnerPropData = Property->Inner->ContainerPtrToValuePtr<uint8>(InnerPropData);
			ValueArray.Emplace(FJsonObjectConverter::UPropertyToJsonValue(Property->Inner, InnerInnerPropData));
		}
	}
	return ValueArray;
}

static TArray<TSharedPtr<FJsonValue>> SerializeSetPropertyAsJsonArray(const void* Data, const UObject* Outer, FSetProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly)
{
	const uint8* PropData = Property->ContainerPtrToValuePtr<uint8>(Data);
	FScriptSetHelper Helper(Property, PropData);
	TArray<TSharedPtr<FJsonValue>> ValueArray;

	FPropertyTest TestProp = FPropertyTest(Property->ElementProp);

	for (FScriptSetHelper::FIterator Iter = Helper.CreateIterator(); Iter; Iter++)
	{
		const uint8* InnerPropData = Helper.GetElementPtr(*Iter);
		if (TestProp.AsArray) // Array
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeArrayPropertyAsJsonArray(InnerPropData, Outer, TestProp.AsArray, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			ValueArray.Emplace(new FJsonValueArray(InnerArray));
		}
		if (TestProp.AsSet) // Set
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeSetPropertyAsJsonArray(InnerPropData, Outer, TestProp.AsSet, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			ValueArray.Emplace(new FJsonValueArray(InnerArray));
		}
		if (TestProp.AsMap) // Map
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeMapPropertyAsJsonArray(InnerPropData, Outer, TestProp.AsMap, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			ValueArray.Emplace(new FJsonValueArray(InnerArray));
		}
		else if (TestProp.AsStruct) // Struct
		{
			TSharedPtr<FJsonObject> StructObject = MakeShareable(new FJsonObject);
			SerializeStructPropertyAsJsonObjectField(InnerPropData, Outer, TestProp.AsStruct, StructObject, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			ValueArray.Emplace(new FJsonValueObject(StructObject));
		}
		else if (TestProp.AsObject) // Object
		{
			const UObject* SubObject = TestProp.AsObject->GetObjectPropertyValue_InContainer(InnerPropData);
			if (SubObject->IsValidLowLevel() && SubObject->GetOuter() == Outer && !TraversedObjects.Contains(SubObject))
			{
				TraversedObjects.Add(SubObject);
				TSharedPtr<FJsonObject> JsonSubObject = MakeShared<FJsonObject>();
				TSharedPtr<FJsonObject> JsonSubObjectProperties = JsonSubObject;

				if (bIncludeObjectClasses) {
					JsonSubObjectProperties = MakeShared<FJsonObject>();
					JsonSubObject->SetStringField(FJsonSerializerFields::ObjectClassField.ToString(), SubObject->GetClass()->GetPathName());
					JsonSubObject->SetObjectField(FJsonSerializerFields::ObjectPropertiesField.ToString(), JsonSubObjectProperties);
				}

				for (TFieldIterator<FProperty> PropertyItr(SubObject->GetClass()); PropertyItr; ++PropertyItr)
				{
					SerializePropertyAsJsonObjectField(SubObject, SubObject, JsonSubObjectProperties, *PropertyItr, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
				}
				ValueArray.Emplace(new FJsonValueObject(JsonSubObject));
			}
			else {
				ValueArray.Emplace(new FJsonValueString(SubObject->GetPathName()));
			}
		}
		else
		{
			TSharedPtr<FJsonValue> JsonValue;
			const uint8* InnerInnerPropData = TestProp.Raw->ContainerPtrToValuePtr<uint8>(InnerPropData);
			ValueArray.Emplace(FJsonObjectConverter::UPropertyToJsonValue(TestProp.Raw, InnerInnerPropData));
		}
	}
	return ValueArray;
}


static TArray<TSharedPtr<FJsonValue>> SerializeMapPropertyAsJsonArray(const void* Data, const UObject* Outer, FMapProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly) {
	
	const uint8* PropData = Property->ContainerPtrToValuePtr<uint8>(Data);
	FScriptMapHelper Helper(Property, PropData);
	TArray<TSharedPtr<FJsonValue>> ValueArray;

	FPropertyTest TestKey = FPropertyTest(Helper.KeyProp);
	FPropertyTest TestValue = FPropertyTest(Helper.ValueProp);

	for (FScriptMapHelper::FIterator Iter = Helper.CreateIterator(); Iter; Iter++) {
		TSharedPtr<FJsonObject> KeyVal = MakeShared< FJsonObject>();

		uint8* KeyData = Helper.GetKeyPtr(*Iter);
		uint8* ValueData = Helper.GetValuePtr(*Iter);

		if (KeyData == nullptr || ValueData == nullptr) continue;

		if (TestKey.AsArray) // Array
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeArrayPropertyAsJsonArray(KeyData, Outer, TestKey.AsArray, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			KeyVal->SetArrayField("Key", InnerArray);
		}
		if (TestKey.AsSet) // Set
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeSetPropertyAsJsonArray(KeyData, Outer, TestKey.AsSet, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			KeyVal->SetArrayField("Key", InnerArray);
		}
		if (TestKey.AsMap) // Map
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeMapPropertyAsJsonArray(KeyData, Outer, TestKey.AsMap, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			KeyVal->SetArrayField("Key", InnerArray);
		}
		else if (TestKey.AsStruct) // Struct
		{
			TSharedPtr<FJsonObject> StructObject = MakeShareable(new FJsonObject);
			SerializeStructPropertyAsJsonObjectField(KeyData, Outer, TestKey.AsStruct, StructObject, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			KeyVal->SetObjectField("Key", StructObject);
		}
		else if (TestKey.AsObject) // Object
		{
			const UObject* SubObject = TestKey.AsObject->GetObjectPropertyValue(KeyData);
			if (SubObject->IsValidLowLevel() && SubObject->GetOuter() == Outer && !TraversedObjects.Contains(SubObject))
			{
				TraversedObjects.Add(SubObject);
				TSharedPtr<FJsonObject> JsonSubObject = MakeShared<FJsonObject>();
				TSharedPtr<FJsonObject> JsonSubObjectProperties = JsonSubObject;

				if (bIncludeObjectClasses) {
					JsonSubObjectProperties = MakeShared<FJsonObject>();
					JsonSubObject->SetStringField(FJsonSerializerFields::ObjectClassField.ToString(), SubObject->GetClass()->GetPathName());
					JsonSubObject->SetObjectField(FJsonSerializerFields::ObjectPropertiesField.ToString(), JsonSubObjectProperties);
				}

				for (TFieldIterator<FProperty> PropertyItr(SubObject->GetClass()); PropertyItr; ++PropertyItr)
				{
					SerializePropertyAsJsonObjectField(SubObject, SubObject, JsonSubObjectProperties, *PropertyItr, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
				}
				KeyVal->SetObjectField("Key", JsonSubObject);
			}
			else {
				KeyVal->SetStringField("Key", SubObject->GetPathName());
			}
		}
		else
		{
			TSharedPtr<FJsonValue> JsonValue;
			KeyVal->SetField("Key", FJsonObjectConverter::UPropertyToJsonValue(TestKey.Raw, KeyData));
		}

		if (TestValue.AsArray) // Array
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeArrayPropertyAsJsonArray(ValueData, Outer, TestValue.AsArray, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			KeyVal->SetArrayField("Value", InnerArray);
		}
		if (TestValue.AsSet) // Set
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeSetPropertyAsJsonArray(ValueData, Outer, TestValue.AsSet, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			KeyVal->SetArrayField("Value", InnerArray);
		}
		if (TestValue.AsMap) // Map
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializeMapPropertyAsJsonArray(ValueData, Outer, TestValue.AsMap, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			KeyVal->SetArrayField("Value", InnerArray);
		}
		else if (TestValue.AsStruct) // Struct
		{
			TSharedPtr<FJsonObject> StructObject = MakeShareable(new FJsonObject);
			SerializeStructPropertyAsJsonObjectField(ValueData, Outer, TestValue.AsStruct, StructObject, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			KeyVal->SetObjectField("Value", StructObject);
		}
		else if (TestValue.AsObject) // Object
		{
			const UObject* SubObject = TestValue.AsObject->GetObjectPropertyValue(ValueData);
			if (SubObject->IsValidLowLevel() && SubObject->GetOuter() == Outer && !TraversedObjects.Contains(SubObject))
			{
				TraversedObjects.Add(SubObject);
				TSharedPtr<FJsonObject> JsonSubObject = MakeShared<FJsonObject>();
				TSharedPtr<FJsonObject> JsonSubObjectProperties = JsonSubObject;

				if (bIncludeObjectClasses) {
					JsonSubObjectProperties = MakeShared<FJsonObject>();
					JsonSubObject->SetStringField(FJsonSerializerFields::ObjectClassField.ToString(), SubObject->GetClass()->GetPathName());
					JsonSubObject->SetObjectField(FJsonSerializerFields::ObjectPropertiesField.ToString(), JsonSubObjectProperties);
				}

				for (TFieldIterator<FProperty> PropertyItr(SubObject->GetClass()); PropertyItr; ++PropertyItr)
				{
					SerializePropertyAsJsonObjectField(SubObject, SubObject, JsonSubObjectProperties, *PropertyItr, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
				}
				KeyVal->SetObjectField("Value", JsonSubObject);
			}
			else {
				KeyVal->SetStringField("Value", SubObject->GetPathName());
			}
		}
		else
		{
			TSharedPtr<FJsonValue> JsonValue;
			KeyVal->SetField("Value", FJsonObjectConverter::UPropertyToJsonValue(TestValue.Raw, ValueData));
		}


		ValueArray.Emplace(new FJsonValueObject(KeyVal));
	}
	
	return ValueArray;
}

static void SerializePropertyAsJsonObjectField(const void* Data, const UObject* Outer, TSharedPtr<FJsonObject> OuterObject, FProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly)
{
	if (Property->GetName() == "UberGraphFrame"
		|| Property->HasAnyPropertyFlags(CPF_Transient)
		/* || (Property->Identical_InContainer(Data, Outer->GetClass()->GetDefaultObject()) && bChangedPropertiesOnly)*/)
	{
		// Don't include "UberGraphFrame" or any transient properties
		return;
	}


	FPropertyTest TestProp = FPropertyTest(Property);

	if (TestProp.AsArray) // Array
	{
		TArray<TSharedPtr<FJsonValue>> Values = SerializeArrayPropertyAsJsonArray(Data, Outer, TestProp.AsArray, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
		OuterObject->SetArrayField(Property->GetAuthoredName(), Values);
	}
	if (TestProp.AsSet) // Set
	{
		TArray<TSharedPtr<FJsonValue>> Values = SerializeSetPropertyAsJsonArray(Data, Outer, TestProp.AsSet, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
		OuterObject->SetArrayField(Property->GetAuthoredName(), Values);
	}
	if (TestProp.AsMap) // Map
	{
		TArray<TSharedPtr<FJsonValue>> Values = SerializeMapPropertyAsJsonArray(Data, Outer, TestProp.AsMap, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
		OuterObject->SetArrayField(Property->GetAuthoredName(), Values);
	}
	else if (TestProp.AsStruct) // Struct
	{
		TSharedPtr<FJsonObject> StructObject = MakeShareable(new FJsonObject);
		SerializeStructPropertyAsJsonObjectField(Data, Outer, TestProp.AsStruct, StructObject, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
		OuterObject->SetObjectField(Property->GetAuthoredName(), StructObject);
	}
	else if (TestProp.AsObject) // Object
	{
		const UObject* SubObject = TestProp.AsObject->GetObjectPropertyValue_InContainer(Data);
		if (SubObject->IsValidLowLevel() && SubObject->GetOuter() == Outer && !TraversedObjects.Contains(SubObject))
		{
			TraversedObjects.Add(SubObject);
			TSharedPtr<FJsonObject> JsonSubObject = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> JsonSubObjectProperties = JsonSubObject;

			if (bIncludeObjectClasses) {
				JsonSubObjectProperties = MakeShared<FJsonObject>();
				JsonSubObject->SetStringField(FJsonSerializerFields::ObjectClassField.ToString(), SubObject->GetClass()->GetPathName());
				JsonSubObject->SetObjectField(FJsonSerializerFields::ObjectPropertiesField.ToString(), JsonSubObjectProperties);
			}

			for (TFieldIterator<FProperty> PropertyItr(SubObject->GetClass()); PropertyItr; ++PropertyItr)
			{
				SerializePropertyAsJsonObjectField(SubObject, SubObject, JsonSubObjectProperties, *PropertyItr, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			}
			OuterObject->SetObjectField(Property->GetAuthoredName(), JsonSubObject);
		}
		else {
			OuterObject->SetStringField(Property->GetAuthoredName(), SubObject->GetPathName());
		}
	}
	else
	{
		TSharedPtr<FJsonValue> JsonValue;
		const uint8* PropData = Property->ContainerPtrToValuePtr<uint8>(Data);
		OuterObject->SetField(Property->GetAuthoredName(), FJsonObjectConverter::UPropertyToJsonValue(Property, PropData));
	}
}

TSharedPtr<FJsonObject> FJsonSerializationModule::SerializeUObjectToJson(const UObject* Object, bool bIncludeObjectClasses, bool bChangedPropertiesOnly)
{
	TSet<const UObject*> TraversedObjects;
	TraversedObjects.Add(Object);

	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> JsonObjectProperties = JsonObject;

	if (bIncludeObjectClasses) {
		JsonObjectProperties = MakeShared<FJsonObject>();
		JsonObject->SetStringField(FJsonSerializerFields::ObjectClassField.ToString(), Object->GetClass()->GetPathName());
		JsonObject->SetObjectField(FJsonSerializerFields::ObjectPropertiesField.ToString(), JsonObjectProperties);
	}

	for (TFieldIterator<FProperty> PropertyItr(Object->GetClass()); PropertyItr; ++PropertyItr)
	{
		SerializePropertyAsJsonObjectField(Object, Object, JsonObjectProperties, *PropertyItr, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
	}

	return JsonObject;
}

// DESERIALIZATION

static bool HasObjectFields(TSharedPtr<FJsonObject> JsonObject);
static void DeserializePropertyFromJsonObjectField(void* Data, UObject* Owner, TSharedPtr<FJsonObject> JsonObjectProperties, FProperty* Property, bool bIncludeObjectClasses);
static void DeserializeArrayPropertyFromJsonObjectField(void* FieldData, UObject* Owner, TArray<TSharedPtr<FJsonValue>> JsonArrayField, FArrayProperty* ArrayProperty, bool bIncludeObjectClasses);
static void DeserializeSetPropertyFromJsonObjectField(void* FieldData, UObject* Owner, TArray<TSharedPtr<FJsonValue>> JsonArrayField, FSetProperty* SetProperty, bool bIncludeObjectClasses);
static void DeserializeMapPropertyFromJsonObjectField(void* FieldData, UObject* Owner, TArray<TSharedPtr<FJsonValue>> JsonArrayField, FMapProperty* MapProperty, bool bIncludeObjectClasses);
static void DeserializeStructPropertyFromJsonObjectField(void* FieldData, UObject* Owner, TSharedPtr<FJsonObject> JsonStructField, FStructProperty* StructProperty, bool bIncludeObjectClasses);


bool HasObjectFields(TSharedPtr<FJsonObject> JsonObject)
{
	return /*JsonObject->HasField(FJsonSerializationModule::ObjectNameField.ToString())
		&& */JsonObject->HasField(FJsonSerializerFields::ObjectClassField.ToString())
		&& JsonObject->HasField(FJsonSerializerFields::ObjectPropertiesField.ToString());
}

static void DeserializeStructPropertyFromJsonObjectField(void* FieldData, UObject* Owner, TSharedPtr<FJsonObject> JsonStructField, FStructProperty* StructProperty, bool bIncludeObjectClasses)
{
	if (StructProperty->Struct == TBaseStructure<FInstancedStruct>::Get())
	{
		FInstancedStruct& InstancedStruct = *(FInstancedStruct*)FieldData;
		for (TFieldIterator<FProperty> PropertyItr(InstancedStruct.GetScriptStruct()); PropertyItr; ++PropertyItr)
		{
			DeserializePropertyFromJsonObjectField(InstancedStruct.GetMutableMemory(), Owner, JsonStructField, *PropertyItr, bIncludeObjectClasses);
		}
	}
	else
	{
		for (TFieldIterator<FProperty> PropertyItr(StructProperty->Struct); PropertyItr; ++PropertyItr)
		{
			DeserializePropertyFromJsonObjectField((void*)FieldData, Owner, JsonStructField, *PropertyItr, bIncludeObjectClasses);
		}
	}
}


static void DeserializeArrayPropertyFromJsonObjectField(void* FieldData, UObject* Owner, TArray<TSharedPtr<FJsonValue>> JsonArrayField, FArrayProperty* ArrayProperty, bool bIncludeObjectClasses) {
	if (FieldData == nullptr
		|| Owner == nullptr
		|| ArrayProperty == nullptr)
	{
		return;
	}


	FScriptArrayHelper Helper(ArrayProperty, FieldData);
	Helper.Resize(0);
	Helper.AddValues(JsonArrayField.Num());

	FPropertyTest TestProperty = FPropertyTest(ArrayProperty->Inner);

	for (int32 i = 0, n = Helper.Num(); i < n; ++i) {

		TSharedPtr<FJsonValue> FieldValue = JsonArrayField[i];
		void* InnerPropData = Helper.GetRawPtr(i);

		if (TestProperty.AsArray) {
			if (FieldValue->Type != EJson::Array) continue;

			DeserializeArrayPropertyFromJsonObjectField(InnerPropData, Owner, FieldValue->AsArray(), TestProperty.AsArray, bIncludeObjectClasses);
		}
		if (TestProperty.AsSet) {
			if (FieldValue->Type != EJson::Array) continue;

			DeserializeSetPropertyFromJsonObjectField(InnerPropData, Owner, FieldValue->AsArray(), TestProperty.AsSet, bIncludeObjectClasses);
		}
		if (TestProperty.AsMap) {
			if (FieldValue->Type != EJson::Array) continue;

			DeserializeMapPropertyFromJsonObjectField(InnerPropData, Owner, FieldValue->AsArray(), TestProperty.AsMap, bIncludeObjectClasses);
		}
		else if (TestProperty.AsStruct) {
			if (FieldValue->Type != EJson::Object) continue;
			DeserializeStructPropertyFromJsonObjectField(InnerPropData, Owner, FieldValue->AsObject(), TestProperty.AsStruct, bIncludeObjectClasses);
		}
		else if (TestProperty.AsObject) {
			if (FieldValue->Type != EJson::Object && FieldValue->Type == EJson::String) continue;

			UObject* SubObject = (UObject*)InnerPropData;
			bool bIsSubObjectToImport = FieldValue->Type == EJson::Object;

			if (bIsSubObjectToImport) {
				FJsonSerializationModule::DeserializeJsonToUObject(SubObject, FieldValue->AsObject(), bIncludeObjectClasses);
			}
			else if (FieldValue->Type == EJson::String) {
				FString ObjectPathName = FieldValue->AsString();
				SubObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPathName);
			}

			if (SubObject != nullptr && bIsSubObjectToImport) {
				SubObject->Rename(nullptr, Owner);
			}

			TestProperty.AsObject->SetPropertyValue(InnerPropData, SubObject);
		}
		else {

			FJsonObjectConverter::JsonValueToUProperty(FieldValue, TestProperty.Raw, InnerPropData);
		}
	}


}

static void DeserializeSetPropertyFromJsonObjectField(void* FieldData, UObject* Owner, TArray<TSharedPtr<FJsonValue>> JsonArrayField, FSetProperty* SetProperty, bool bIncludeObjectClasses) {
	if (FieldData == nullptr
		|| Owner == nullptr
		|| SetProperty == nullptr)
	{
		return;
	}


	FScriptSetHelper Helper(SetProperty, FieldData);
	Helper.EmptyElements(JsonArrayField.Num());
	FPropertyTest TestProperty = FPropertyTest(SetProperty->ElementProp);

	for (int32 i = 0, n = JsonArrayField.Num(); i < n; ++i) {

		TSharedPtr<FJsonValue> FieldValue = JsonArrayField[i];

		int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
		void* InnerPropData = Helper.GetElementPtr(NewIndex);

		if (TestProperty.AsArray) {
			if (FieldValue->Type != EJson::Array) continue;

			DeserializeArrayPropertyFromJsonObjectField(InnerPropData, Owner, FieldValue->AsArray(), TestProperty.AsArray, bIncludeObjectClasses);
		}
		if (TestProperty.AsSet) {
			if (FieldValue->Type != EJson::Array) continue;

			DeserializeSetPropertyFromJsonObjectField(InnerPropData, Owner, FieldValue->AsArray(), TestProperty.AsSet, bIncludeObjectClasses);
		}
		if (TestProperty.AsMap) {
			if (FieldValue->Type != EJson::Array) continue;

			DeserializeMapPropertyFromJsonObjectField(InnerPropData, Owner, FieldValue->AsArray(), TestProperty.AsMap, bIncludeObjectClasses);
		}
		else if (TestProperty.AsStruct) {
			if (FieldValue->Type != EJson::Object) continue;
			DeserializeStructPropertyFromJsonObjectField(InnerPropData, Owner, FieldValue->AsObject(), TestProperty.AsStruct, bIncludeObjectClasses);
		}
		else if (TestProperty.AsObject) {
			if (FieldValue->Type != EJson::Object && FieldValue->Type == EJson::String) continue;

			UObject* SubObject = (UObject*)InnerPropData;
			bool bIsSubObjectToImport = FieldValue->Type == EJson::Object;

			if (bIsSubObjectToImport) {
				FJsonSerializationModule::DeserializeJsonToUObject(SubObject, FieldValue->AsObject(), bIncludeObjectClasses);
			}
			else if (FieldValue->Type == EJson::String) {
				FString ObjectPathName = FieldValue->AsString();
				SubObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPathName);
			}

			if (SubObject != nullptr && bIsSubObjectToImport) {
				SubObject->Rename(nullptr, Owner);
			}

			TestProperty.AsObject->SetPropertyValue(InnerPropData, SubObject);
		}
		else {

			FJsonObjectConverter::JsonValueToUProperty(FieldValue, TestProperty.Raw, InnerPropData);
		}
	}

	Helper.Rehash();
}

void DeserializeMapPropertyFromJsonObjectField(void* FieldData, UObject* Owner, TArray<TSharedPtr<FJsonValue>> JsonArrayField, FMapProperty* MapProperty, bool bIncludeObjectClasses)
{
	if (FieldData == nullptr
		|| Owner == nullptr
		|| MapProperty == nullptr)
	{
		return;
	}


	FScriptMapHelper Helper(MapProperty, FieldData);
	Helper.EmptyValues(JsonArrayField.Num());

	FPropertyTest TestKey = FPropertyTest(Helper.KeyProp);
	FPropertyTest TestValue = FPropertyTest(Helper.ValueProp);

	for (int32 i = 0, n = JsonArrayField.Num(); i < n; ++i) {

		TSharedPtr<FJsonValue> FieldValue = JsonArrayField[i];

		if (FieldValue->Type != EJson::Object) continue;

		TSharedPtr<FJsonObject> KeyValObject = FieldValue->AsObject();
		TSharedPtr<FJsonValue> KeyValue = KeyValObject->GetField<EJson::None>("Key");
		TSharedPtr<FJsonValue> ValValue = KeyValObject->GetField<EJson::None>("Value");

		int32 NewIndex = Helper.AddDefaultValue_Invalid_NeedsRehash();
		void* KeyData = Helper.GetKeyPtr(NewIndex);
		void* ValueData = Helper.GetValuePtr(NewIndex);

		if (TestKey.AsArray) {
			if (KeyValue->Type != EJson::Array) continue;

			DeserializeArrayPropertyFromJsonObjectField(KeyData, Owner, KeyValue->AsArray(), TestKey.AsArray, bIncludeObjectClasses);
		}
		if (TestKey.AsSet) {
			if (KeyValue->Type != EJson::Array) continue;

			DeserializeSetPropertyFromJsonObjectField(KeyData, Owner, KeyValue->AsArray(), TestKey.AsSet, bIncludeObjectClasses);
		}
		if (TestKey.AsMap) {
			if (KeyValue->Type != EJson::Array) continue;

			DeserializeMapPropertyFromJsonObjectField(KeyData, Owner, KeyValue->AsArray(), TestKey.AsMap, bIncludeObjectClasses);
		}
		else if (TestKey.AsStruct) {
			if (KeyValue->Type != EJson::Object) continue;
			DeserializeStructPropertyFromJsonObjectField(KeyData, Owner, KeyValue->AsObject(), TestKey.AsStruct, bIncludeObjectClasses);
		}
		else if (TestKey.AsObject) {
			if (KeyValue->Type != EJson::Object && KeyValue->Type != EJson::String) continue;

			UObject* SubObject = (UObject*)KeyData;
			bool bIsSubObjectToImport = KeyValue->Type == EJson::Object;

			if (bIsSubObjectToImport) {
				FJsonSerializationModule::DeserializeJsonToUObject(SubObject, KeyValue->AsObject(), bIncludeObjectClasses);
			}
			else if (KeyValue->Type == EJson::String) {
				FString ObjectPathName = KeyValue->AsString();
				SubObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPathName);
			}

			if (SubObject != nullptr && bIsSubObjectToImport) {
				SubObject->Rename(nullptr, Owner);
			}

			TestKey.AsObject->SetPropertyValue(KeyData, SubObject);
		}
		else {

			FJsonObjectConverter::JsonValueToUProperty(KeyValue, TestKey.Raw, KeyData);
		}


		if (TestValue.AsArray) {
			if (ValValue->Type != EJson::Array) continue;

			DeserializeArrayPropertyFromJsonObjectField(ValueData, Owner, ValValue->AsArray(), TestValue.AsArray, bIncludeObjectClasses);
		}
		if (TestValue.AsSet) {
			if (ValValue->Type != EJson::Array) continue;

			DeserializeSetPropertyFromJsonObjectField(ValueData, Owner, ValValue->AsArray(), TestValue.AsSet, bIncludeObjectClasses);
		}
		if (TestValue.AsMap) {
			if (ValValue->Type != EJson::Array) continue;

			DeserializeMapPropertyFromJsonObjectField(ValueData, Owner, ValValue->AsArray(), TestValue.AsMap, bIncludeObjectClasses);
		}
		else if (TestValue.AsStruct) {
			if (ValValue->Type != EJson::Object) continue;
			DeserializeStructPropertyFromJsonObjectField(ValueData, Owner, ValValue->AsObject(), TestValue.AsStruct, bIncludeObjectClasses);
		}
		else if (TestValue.AsObject) {
			if (ValValue->Type != EJson::Object && ValValue->Type != EJson::String) continue;

			UObject* SubObject = (UObject*)ValueData;
			bool bIsSubObjectToImport = ValValue->Type == EJson::Object;

			if (bIsSubObjectToImport) {
				FJsonSerializationModule::DeserializeJsonToUObject(SubObject, ValValue->AsObject(), bIncludeObjectClasses);
			}
			else if (ValValue->Type == EJson::String) {
				FString ObjectPathName = ValValue->AsString();
				SubObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPathName);
			}

			if (SubObject != nullptr && bIsSubObjectToImport) {
				SubObject->Rename(nullptr, Owner);
			}

			TestValue.AsObject->SetPropertyValue(ValueData, SubObject);
		}
		else {

			FJsonObjectConverter::JsonValueToUProperty(ValValue, TestValue.Raw, ValueData);
		}
	}

	Helper.Rehash();
}

static void DeserializePropertyFromJsonObjectField(void* Data, UObject* Owner, TSharedPtr<FJsonObject> JsonObjectProperties, FProperty* Property, bool bIncludeObjectClasses) {
	if (Data == nullptr
		||Owner == nullptr
		|| JsonObjectProperties == nullptr
		|| Property == nullptr) 
	{
		return;
	}

	FPropertyTest TestProperty = FPropertyTest(Property);

	const FString PropertyName = Property->GetAuthoredName();

	if (!JsonObjectProperties->HasField(PropertyName)) return;

	TSharedPtr<FJsonValue> FieldValue = JsonObjectProperties->GetField<EJson::None>(PropertyName);
	void* FieldData = Property->ContainerPtrToValuePtr<void>(Data);

	if (TestProperty.AsArray){
		if (FieldValue->Type == EJson::Array) {
			TArray<TSharedPtr<FJsonValue>> ArrayJson = FieldValue->AsArray();
			DeserializeArrayPropertyFromJsonObjectField(FieldData, Owner, ArrayJson, TestProperty.AsArray, bIncludeObjectClasses);
		}
	}
	if (TestProperty.AsSet) {
		if (FieldValue->Type == EJson::Array) {
			TArray<TSharedPtr<FJsonValue>> ArrayJson = FieldValue->AsArray();
			DeserializeSetPropertyFromJsonObjectField(FieldData, Owner, ArrayJson, TestProperty.AsSet, bIncludeObjectClasses);
		}
	}
	if (TestProperty.AsMap) {
		if (FieldValue->Type == EJson::Array) {
			TArray<TSharedPtr<FJsonValue>> ArrayJson = FieldValue->AsArray();
			DeserializeMapPropertyFromJsonObjectField(FieldData, Owner, ArrayJson, TestProperty.AsMap, bIncludeObjectClasses);
		}
	}
	else if (TestProperty.AsStruct) {
		if (FieldValue->Type != EJson::Object) return;
		DeserializeStructPropertyFromJsonObjectField(FieldData, Owner, FieldValue->AsObject(), TestProperty.AsStruct, bIncludeObjectClasses);
	}
	else if (TestProperty.AsObject) {
		UObject* SubObject = (UObject*)FieldData;

		bool bIsSubObjectToImport = FieldValue->Type == EJson::Object;

		if (bIsSubObjectToImport) {
			FJsonSerializationModule::DeserializeJsonToUObject(SubObject, FieldValue->AsObject(), bIncludeObjectClasses);
		}
		else if (FieldValue->Type == EJson::String){
			FString ObjectPathName = FieldValue->AsString();
			SubObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPathName);
		}

		if (SubObject != nullptr && bIsSubObjectToImport) {
			SubObject->Rename(nullptr, Owner);
		}

		TestProperty.AsObject->SetPropertyValue(FieldData, SubObject);
	}
	else {
		
		FJsonObjectConverter::JsonValueToUProperty(FieldValue, Property, FieldData);
	}
}

void FJsonSerializationModule::DeserializeJsonToUObject(UObject*& Object, TSharedPtr<FJsonObject> JsonObject, bool bIncludeObjectClasses)
{
	if (Object == nullptr && !bIncludeObjectClasses) return;

	TSharedPtr<FJsonObject> JsonObjectProperties = JsonObject;
	if (bIncludeObjectClasses) {
		if (!JsonObject->HasTypedField<EJson::Object>(FJsonSerializerFields::ObjectPropertiesField.ToString())) return;
		JsonObjectProperties = JsonObject->GetObjectField(FJsonSerializerFields::ObjectPropertiesField.ToString());

		FString ClassPathName = JsonObject->GetStringField(FJsonSerializerFields::ObjectClassField.ToString());
		UClass* ObjectClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPathName);
		
		if (ObjectClass != nullptr && (Object == nullptr || Object->GetClass() == ObjectClass)) {

			Object = NewObject<UObject>(GetTransientPackage(), ObjectClass);

			if (Object == nullptr) return;
		}
	}

	for (TFieldIterator<FProperty> PropertyItr(Object->GetClass()); PropertyItr; ++PropertyItr)
	{
		DeserializePropertyFromJsonObjectField(Object, Object, JsonObjectProperties, *PropertyItr, bIncludeObjectClasses);
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FJsonSerializationModule, JsonSerialization)