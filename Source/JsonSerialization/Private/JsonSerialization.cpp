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

static void SerializePropertyAsJsonObjectField(const void* Data, const UObject* Outer, TSharedPtr<FJsonObject> OuterObject, FProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly);
static void SerializeStructPropertyAsJsonObjectField(const void* InnerPropData, const UObject* Outer, FStructProperty* StructProperty, TSharedPtr<FJsonObject> StructObject, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly);
static TArray<TSharedPtr<FJsonValue>> SerializePropertyAsJsonArray(const void* Data, const UObject* Outer, FArrayProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly);

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

static TArray<TSharedPtr<FJsonValue>> SerializePropertyAsJsonArray(const void* Data, const UObject* Outer, FArrayProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly)
{
	const uint8* PropData = Property->ContainerPtrToValuePtr<uint8>(Data);
	FScriptArrayHelper Helper(Property, PropData);
	TArray<TSharedPtr<FJsonValue>> ValueArray;

	for (int32 i = 0, n = Helper.Num(); i < n; ++i)
	{
		const uint8* InnerPropData = Helper.GetRawPtr(i);
		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property->Inner)) // Array
		{
			TArray<TSharedPtr<FJsonValue>> InnerArray = SerializePropertyAsJsonArray(InnerPropData, Outer, ArrayProperty, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			ValueArray.Emplace(new FJsonValueArray(InnerArray));
		}
		else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property->Inner)) // Struct
		{
			TSharedPtr<FJsonObject> StructObject = MakeShareable(new FJsonObject);
			SerializeStructPropertyAsJsonObjectField(InnerPropData, Outer, StructProperty, StructObject, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
			ValueArray.Emplace(new FJsonValueObject(StructObject));
		}
		else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property->Inner)) // Object
		{
			const UObject* SubObject = ObjectProperty->GetObjectPropertyValue_InContainer(InnerPropData);
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

static void SerializePropertyAsJsonObjectField(const void* Data, const UObject* Outer, TSharedPtr<FJsonObject> OuterObject, FProperty* Property, TSet<const UObject*>& TraversedObjects, bool bIncludeObjectClasses, bool bChangedPropertiesOnly)
{
	if (Property->GetName() == "UberGraphFrame"
		|| Property->HasAnyPropertyFlags(CPF_Transient)
		/* || (Property->Identical_InContainer(Data, Outer->GetClass()->GetDefaultObject()) && bChangedPropertiesOnly)*/)
	{
		// Don't include "UberGraphFrame" or any transient properties
		return;
	}

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property)) // Array
	{
		TArray<TSharedPtr<FJsonValue>> Values = SerializePropertyAsJsonArray(Data, Outer, ArrayProperty, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
		OuterObject->SetArrayField(Property->GetAuthoredName(), Values);
	}
	else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property)) // Struct
	{
		TSharedPtr<FJsonObject> StructObject = MakeShareable(new FJsonObject);
		SerializeStructPropertyAsJsonObjectField(Data, Outer, StructProperty, StructObject, TraversedObjects, bIncludeObjectClasses, bChangedPropertiesOnly);
		OuterObject->SetObjectField(Property->GetAuthoredName(), StructObject);
	}
	else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property)) // Object
	{
		const UObject* SubObject = ObjectProperty->GetObjectPropertyValue_InContainer(Data);
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
	Helper.AddValues(JsonArrayField.Num());

	FProperty* InnerProperty = ArrayProperty->Inner;


	FArrayProperty* TestArrayProperty = CastField< FArrayProperty>(InnerProperty);
	FObjectProperty* TestObjectProperty = CastField< FObjectProperty>(InnerProperty);
	FStructProperty* TestStructProperty = CastField< FStructProperty>(InnerProperty);

	for (int32 i = 0, n = Helper.Num(); i < n; ++i) {

		TSharedPtr<FJsonValue> FieldValue = JsonArrayField[i];
		void* InnerPropData = Helper.GetRawPtr(i);

		if (TestArrayProperty) {
			if (FieldValue->Type != EJson::Array) continue;

			DeserializeArrayPropertyFromJsonObjectField(InnerPropData, Owner, FieldValue->AsArray(), TestArrayProperty, bIncludeObjectClasses);
		}
		else if (TestStructProperty) {
			if (FieldValue->Type != EJson::Object) continue;
			DeserializeStructPropertyFromJsonObjectField(InnerPropData, Owner, FieldValue->AsObject(), TestStructProperty, bIncludeObjectClasses);
		}
		else if (TestObjectProperty) {
			if (FieldValue->Type != EJson::Object) continue;

			UObject* SubObject = (UObject*)InnerPropData;

			if (FieldValue->Type == EJson::Object) {
				FJsonSerializationModule::DeserializeJsonToUObject(SubObject, FieldValue->AsObject(), bIncludeObjectClasses);
			}

			if (SubObject != nullptr) {
				SubObject->Rename(nullptr, Owner);
			}

			TestObjectProperty->SetPropertyValue(InnerPropData, SubObject);
		}
		else {

			FJsonObjectConverter::JsonValueToUProperty(FieldValue, InnerProperty, InnerPropData);
		}
	}


}

static void DeserializePropertyFromJsonObjectField(void* Data, UObject* Owner, TSharedPtr<FJsonObject> JsonObjectProperties, FProperty* Property, bool bIncludeObjectClasses) {
	if (Data == nullptr
		||Owner == nullptr
		|| JsonObjectProperties == nullptr
		|| Property == nullptr) 
	{
		return;
	}

	const FString PropertyName = Property->GetAuthoredName();

	if (!JsonObjectProperties->HasField(PropertyName)) return;

	TSharedPtr<FJsonValue> FieldValue = JsonObjectProperties->GetField<EJson::None>(PropertyName);
	void* FieldData = Property->ContainerPtrToValuePtr<void>(Data);

	if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property)){
		if (FieldValue->Type == EJson::Array) {
			TArray<TSharedPtr<FJsonValue>> ArrayJson = FieldValue->AsArray();
			DeserializeArrayPropertyFromJsonObjectField(FieldData, Owner, ArrayJson, ArrayProperty, bIncludeObjectClasses);
		}
	}
	else if (FStructProperty* StructProperty = CastField<FStructProperty>(Property)) {
		if (FieldValue->Type != EJson::Object) return;
		DeserializeStructPropertyFromJsonObjectField(FieldData, Owner, FieldValue->AsObject(), StructProperty, bIncludeObjectClasses);
	}
	else if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property)) {
		UObject* SubObject = (UObject*)FieldData;

		if (FieldValue->Type == EJson::Object) {
			FJsonSerializationModule::DeserializeJsonToUObject(SubObject, FieldValue->AsObject(), bIncludeObjectClasses);
		}

		if (SubObject != nullptr) {
			SubObject->Rename(nullptr, Owner);
		}

		ObjectProperty->SetPropertyValue(FieldData, SubObject);
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

		
		if (Object == nullptr) {
			FString ClassPathName = JsonObject->GetStringField(FJsonSerializerFields::ObjectClassField.ToString());
			UClass* ObjectClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPathName);

			if (ObjectClass == nullptr) return;

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