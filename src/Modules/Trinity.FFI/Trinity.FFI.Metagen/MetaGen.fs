﻿namespace Trinity.FFI.Metagen

open Trinity.FFI.Metagen

module MetaGen = 
    open GraphEngine.Jit
    open GraphEngine.Jit.Verbs
    open GraphEngine.Jit.TypeSystem
    open System
    open Trinity.Storage
    open Trinity.FFI.Metagen.Operator

    type SwigCode      = string
    type CodeGenerator = (string -> string)

    
    let mangling (manglingCode: char) (name: string) = name.Replace(manglingCode, manglingCode + manglingCode)
         
    let rec make'name (manglingCode: char) (desc: TypeDescriptor) = 
        (** transform a typedescriptor into a name signature 
            
            eg. List<List<int>> ->
                List_List_int (if `_` is the mangling code
                
                List<My_Struct> ->
                List_My__Struct(if `_` is the mangling code
        **)
        let m_mangling = mangling manglingCode
        
        match desc with
        | {TypeCode=LIST; ElementType=elemType}  -> 
                let elemTypeName = elemType |> Seq.head |> (make'name manglingCode)
                PString.format "List{_}{elem}"          ["_" ->> manglingCode; "elem"       ->> elemTypeName]
        | {TypeCode=CELL; TypeName=cellName}     ->
                PString.format "Cell{_}{cellName}"      ["_" ->> manglingCode; "cellName"   ->> m_mangling cellName]
        | {TypeCode=STRUCT; TypeName=structName} ->
                 PString.format "Struct{_}{structName}" ["_" ->> manglingCode; "structName" ->> m_mangling structName]
        | _                                      ->
                desc.TypeName.ToLower() // primitive type
    
    let render'operations (render : TypeDescriptor -> Verb -> 'T ) (type': TypeDescriptor) : seq<'T> =
        match type' with
        | {TypeCode=CELL; Members=members}
        | {TypeCode=STRUCT; Members=members}    ->
           members
           |> Seq.collect (
                fun (member': MemberDescriptor) ->
                    let fieldName   = member'.Name
                    [BGet; BSet; SGet fieldName; SSet fieldName; ComposedVerb(SGet fieldName, BGet)]
                    |> Seq.map (render type'))
           
        
        | {TypeCode=LIST;ElementType=elemTypes}  ->
             [BGet; BSet; LGet; LSet; LContains; LCount; ComposedVerb(LGet, BGet);] 
             |> Seq.map (render type')
        | _                                      ->
             (** primitive type **)
             seq [BGet; BSet;]
             |> Seq.map (render type')
    
    let rec TypeInfer(anyType: TypeDescriptor): seq<TypeDescriptor> = 
    (** inference out the descriptors of struct types and generic list types in a cell descriptor.**)
        match anyType with 
        | {TypeCode=LIST; ElementType = elemType} -> 
                anyType >>> TypeInfer (Seq.head elemType)

        | {TypeCode=CELL; Members=members}
        | {TypeCode=STRUCT; Members=members}      -> 
                members
                |> Seq.collect (fun field -> field.Type |> TypeInfer)
                |> fun tail -> anyType >>> tail
        | _                                       -> 
                (** primitive type **)
                seq[anyType]
   

    let Generate (render : TypeDescriptor -> Verb -> 'T) 
                 (schema : IStorageSchema)
                 : seq<TypeDescriptor * seq<'T>> =
        schema.CellDescriptors
        |> Seq.map Make
        |> Seq.collect TypeInfer
        |> Seq.distinct
        |> Seq.map (fun typeDesc ->
                typeDesc
                |> render'operations render
                |> fun methods   -> (typeDesc, methods))
        

    
    let GenerateSwig   (manglingCode: char) = SwigGen.render   manglingCode make'name |> Generate
    let GenerateCSharp (manglingCode: char) = CSharpGen.render manglingCode make'name |> Generate
    let GenerateJit    (manglingCode: char) = JitGen.render    manglingCode make'name |> Generate

    let GenerateSwigJit (manglingCode: char) = 
        
        let swig = SwigGen.render manglingCode make'name
        let jit  = JitGen.render manglingCode make'name

        let new'render (type': TypeDescriptor) (verb: Verb) =
            let swig'result = swig type' verb 
            let jit'result  = jit  type' verb
            jit'result, swig'result
        Generate new'render
   
    open Trinity.FFI.Metagen.CodeGen
    let CodeGenSwigJit (manglingCode: char) (schema: IStorageSchema)= 
        GenerateSwigJit manglingCode schema
        |> CodeGen.generateSwigFile manglingCode make'name

        
