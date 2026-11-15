use automerge::{
    marks::{ExpandMark, Mark},
    sync::{Capability, Message, MessageVersion, State, SyncDoc},
    transaction::{CommitOptions, Transactable},
    ActorId, AutoCommit, ObjType, ReadDoc, ScalarValue, ROOT,
};
use std::env;
use std::fs;
use std::path::{Path, PathBuf};

const COMPATIBILITY_CASES: &[&str] = &[
    "scalar_values",
    "nested_objects",
    "list_edits",
    "unicode_text",
    "marks",
    "counter_increments",
    "deletes_tombstones",
    "concurrent_conflicts",
    "commit_metadata",
    "long_history",
    "interleaved_histories",
    "empty_document",
];

const CPP_CASES: &[&str] = &[
    "scalar_values",
    "nested_objects",
    "list_edits",
    "unicode_text",
    "marks",
    "counter_increments",
    "deletes_tombstones",
    "concurrent_conflicts",
    "commit_metadata",
    "long_history",
    "interleaved_histories",
    "empty_document",
];

fn actor(bytes: u8) -> ActorId {
    ActorId::from([bytes; 16])
}

fn fixture_directory() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .to_path_buf()
}

fn write_fixture_with_extension(name: &str, extension: &str, document: &mut AutoCommit) {
    let directory = fixture_directory();
    let saved_bytes = document.save_after(&[]);
    let hashes = document
        .get_changes(&[])
        .iter()
        .map(|change| change.hash().to_string())
        .collect::<Vec<_>>()
        .join("\n");

    fs::write(directory.join(format!("{name}.{extension}")), saved_bytes).unwrap();
    fs::write(
        directory.join(format!("{name}.hashes")),
        if hashes.is_empty() {
            String::new()
        } else {
            format!("{hashes}\n")
        },
    )
    .unwrap();
    fs::write(
        directory.join(format!("{name}.document.{extension}")),
        document.save(),
    )
    .unwrap();
}

fn write_fixture(name: &str, document: &mut AutoCommit) {
    write_fixture_with_extension(name, "am", document);
}

fn write_compatibility_fixture(name: &str, document: &mut AutoCommit) {
    write_fixture_with_extension(name, "automerge", document);
}

fn map_puts() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x11));
    document.put(ROOT, "title", "Automerge").unwrap();
    document.put(ROOT, "version", 1).unwrap();
    document.commit();
    document
}

fn list_inserts() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x22));
    let list = document.put_object(ROOT, "items", ObjType::List).unwrap();
    document.insert(&list, 0, "first").unwrap();
    document.insert(&list, 1, "second").unwrap();
    document.commit();
    document
}

fn text_splice() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x33));
    let text = document.put_object(ROOT, "body", ObjType::Text).unwrap();
    document.splice_text(&text, 0, 0, "hello").unwrap();
    document.splice_text(&text, 1, 3, "i").unwrap();
    document.commit();
    document
}

fn two_actor_merge() -> AutoCommit {
    let mut first = AutoCommit::new().with_actor(actor(0x44));
    first.put(ROOT, "shared", "base").unwrap();
    first.commit();

    let mut second = first.fork().with_actor(actor(0x55));
    first.put(ROOT, "left", true).unwrap();
    first.commit();
    second.put(ROOT, "right", true).unwrap();
    second.commit();
    first.merge(&mut second).unwrap();
    first
}

fn scalar_values() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x81));
    document.put(ROOT, "null", ScalarValue::Null).unwrap();
    document.put(ROOT, "bool", true).unwrap();
    document.put(ROOT, "int", -42_i64).unwrap();
    document.put(ROOT, "uint", 42_u64).unwrap();
    document.put(ROOT, "f64", 3.5_f64).unwrap();
    document.put(ROOT, "str", "value").unwrap();
    document
        .put(
            ROOT,
            "bytes",
            ScalarValue::Bytes(vec![0x00, 0x01, 0xfe, 0xff]),
        )
        .unwrap();
    document
        .put(ROOT, "counter", ScalarValue::Counter(7_i64.into()))
        .unwrap();
    document
        .put(ROOT, "timestamp", ScalarValue::Timestamp(1_700_000_000_123))
        .unwrap();
    document.commit();
    document
}

fn nested_objects() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x82));
    let map = document.put_object(ROOT, "profile", ObjType::Map).unwrap();
    document.put(&map, "name", "Ada").unwrap();
    let list = document.put_object(&map, "tags", ObjType::List).unwrap();
    document.insert(&list, 0, "math").unwrap();
    let entry = document.insert_object(&list, 1, ObjType::Map).unwrap();
    document.put(&entry, "language", "C++").unwrap();
    document.commit();
    document
}

fn list_edits() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x83));
    let list = document.put_object(ROOT, "items", ObjType::List).unwrap();
    document.insert(&list, 0, "a").unwrap();
    document.insert(&list, 1, "b").unwrap();
    document.insert(&list, 2, "c").unwrap();
    document.commit();

    document.put(&list, 1, "B").unwrap();
    document.delete(&list, 0).unwrap();
    document.insert(&list, 1, "between").unwrap();
    document.commit();
    document
}

fn unicode_text() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x84));
    let text = document.put_object(ROOT, "body", ObjType::Text).unwrap();
    document.splice_text(&text, 0, 0, "Aé🙂漢字").unwrap();
    document.commit();
    document.splice_text(&text, 1, 2, "🚀ß").unwrap();
    document.commit();
    document
}

fn marks() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x85));
    let text = document.put_object(ROOT, "body", ObjType::Text).unwrap();
    document.splice_text(&text, 0, 0, "marked text").unwrap();
    document.commit();
    document
        .mark(
            &text,
            Mark::new("bold".to_string(), true, 0, 6),
            ExpandMark::Both,
        )
        .unwrap();
    document.commit();
    document
}

fn counter_increments() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x86));
    document
        .put(ROOT, "count", ScalarValue::Counter(10_i64.into()))
        .unwrap();
    document.commit();
    document.increment(ROOT, "count", 5).unwrap();
    document.commit();
    document.increment(ROOT, "count", -3).unwrap();
    document.commit();
    document
}

fn deletes_tombstones() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x87));
    document.put(ROOT, "gone", "value").unwrap();
    let list = document.put_object(ROOT, "items", ObjType::List).unwrap();
    document.insert(&list, 0, "first").unwrap();
    document.insert(&list, 1, "deleted").unwrap();
    document.insert(&list, 2, "last").unwrap();
    document.commit();

    document.delete(ROOT, "gone").unwrap();
    document.delete(&list, 1).unwrap();
    document.commit();
    document.insert(&list, 1, "replacement").unwrap();
    document.commit();
    document
}

fn concurrent_conflicts() -> AutoCommit {
    let mut first = AutoCommit::new().with_actor(actor(0x88));
    first.put(ROOT, "value", "base").unwrap();
    first.commit();
    let mut second = first.fork().with_actor(actor(0x89));

    first.put(ROOT, "value", "left").unwrap();
    first.commit();
    second.put(ROOT, "value", "right").unwrap();
    second.commit();
    first.merge(&mut second).unwrap();
    first
}

fn commit_metadata() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x77));
    document.put(ROOT, "metadata", true).unwrap();
    document.commit_with(CommitOptions {
        message: Some("import customer record".to_owned()),
        time: Some(1_725_000_000_123),
    });
    document
}

fn long_history() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x8b));
    for index in 0..1_000 {
        document
            .put(ROOT, format!("key-{index:04}"), index as i64)
            .unwrap();
        document.commit();
    }
    document
}

fn interleaved_histories() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x8c));
    document.put(ROOT, "round-00", "first").unwrap();
    document.commit();

    for round in 1..=10 {
        document.set_actor(actor(0x8c));
        document
            .put(ROOT, format!("round-{round:02}-first"), round as i64)
            .unwrap();
        document.commit();
        document.set_actor(actor(0x8d));
        document
            .put(ROOT, format!("round-{round:02}-second"), -(round as i64))
            .unwrap();
        document.commit();
    }
    document
}

fn empty_document() -> AutoCommit {
    AutoCommit::new().with_actor(actor(0x8e))
}

fn compatibility_document(name: &str) -> AutoCommit {
    match name {
        "scalar_values" => scalar_values(),
        "nested_objects" => nested_objects(),
        "list_edits" => list_edits(),
        "unicode_text" => unicode_text(),
        "marks" => marks(),
        "counter_increments" => counter_increments(),
        "deletes_tombstones" => deletes_tombstones(),
        "concurrent_conflicts" => concurrent_conflicts(),
        "commit_metadata" => commit_metadata(),
        "long_history" => long_history(),
        "interleaved_histories" => interleaved_histories(),
        "empty_document" => empty_document(),
        _ => panic!("unknown compatibility fixture: {name}"),
    }
}

fn write_incremental_fixture() {
    let directory = fixture_directory();
    let mut document = AutoCommit::new().with_actor(actor(0x66));

    document.put(ROOT, "stage", "b".repeat(400)).unwrap();
    document.commit();
    fs::write(directory.join("incremental_base.am"), document.save()).unwrap();

    document.put(ROOT, "first", 1).unwrap();
    document.commit();
    fs::write(
        directory.join("incremental_first.am"),
        document.save_incremental(),
    )
    .unwrap();

    document.put(ROOT, "second", 2).unwrap();
    document.commit();
    fs::write(
        directory.join("incremental_second.am"),
        document.save_incremental(),
    )
    .unwrap();
}

fn write_sync_document(path: &Path, document: &mut AutoCommit) {
    fs::write(path, document.save_after(&[])).unwrap();
}

fn encode_v1(message: Message) -> Vec<u8> {
    let mut message = message;
    message.version = MessageVersion::V1;
    message.supported_capabilities = Some(vec![Capability::MessageV1]);
    message.encode()
}

fn sync_exchange() {
    let exchange_directory = fixture_directory().join("sync_exchange");
    if exchange_directory.exists() {
        fs::remove_dir_all(&exchange_directory).unwrap();
    }
    fs::create_dir(&exchange_directory).unwrap();

    let mut first = AutoCommit::new().with_actor(actor(0x66));
    first.put(ROOT, "first", "one").unwrap();
    first.commit();
    first.put(ROOT, "second", 2).unwrap();
    first.commit();

    let mut second = AutoCommit::new().with_actor(actor(0x77));
    second.put(ROOT, "remote", true).unwrap();
    second.commit();

    write_sync_document(&exchange_directory.join("first.am"), &mut first);
    write_sync_document(&exchange_directory.join("second.am"), &mut second);

    let mut first_state = State::new();
    let mut second_state = State::new();
    let mut message_number = 0;

    loop {
        let first_message = first.sync().generate_sync_message(&mut first_state);
        let second_message = second.sync().generate_sync_message(&mut second_state);
        if first_message.is_none() && second_message.is_none() {
            break;
        }
        assert!(message_number < 20, "sync exchange did not converge");

        if let Some(message) = first_message {
            let bytes = encode_v1(message);
            let decoded = Message::decode(&bytes).unwrap();
            let filename = format!("{message_number:02}-first-to-second.sync");
            fs::write(exchange_directory.join(filename), bytes).unwrap();
            second
                .sync()
                .receive_sync_message(&mut second_state, decoded)
                .unwrap();
            message_number += 1;
        }

        if let Some(message) = second_message {
            let bytes = encode_v1(message);
            let decoded = Message::decode(&bytes).unwrap();
            let filename = format!("{message_number:02}-second-to-first.sync");
            fs::write(exchange_directory.join(filename), bytes).unwrap();
            first
                .sync()
                .receive_sync_message(&mut first_state, decoded)
                .unwrap();
            message_number += 1;
        }
    }

    assert_eq!(first.get_heads(), second.get_heads());
}

fn generate() {
    write_fixture("map_puts", &mut map_puts());
    write_fixture("list_inserts", &mut list_inserts());
    write_fixture("text_splice", &mut text_splice());
    write_fixture("two_actor_merge", &mut two_actor_merge());
    write_fixture("commit_metadata", &mut commit_metadata());
    for name in COMPATIBILITY_CASES {
        write_compatibility_fixture(name, &mut compatibility_document(name));
    }
    write_incremental_fixture();
    sync_exchange();
}

fn load_document(path: &Path) -> AutoCommit {
    let bytes = fs::read(path).unwrap_or_else(|error| panic!("{}: {error}", path.display()));
    AutoCommit::load(&bytes).unwrap_or_else(|error| panic!("{}: {error}", path.display()))
}

fn verify_cpp(output_directory: &Path) {
    for name in CPP_CASES {
        let expected_change_path = if *name == "empty_document" {
            fixture_directory().join("empty_document.document.automerge")
        } else {
            fixture_directory().join(format!("{name}.automerge"))
        };
        verify_cpp_fixture(
            name,
            &output_directory.join(format!("{name}.automerge")),
            &expected_change_path,
        );
        verify_cpp_fixture(
            name,
            &output_directory.join(format!("{name}.document.automerge")),
            &fixture_directory().join(format!("{name}.document.automerge")),
        );
    }
}

fn verify_cpp_fixture(name: &str, actual_path: &Path, expected_path: &Path) {
    let actual = load_document(actual_path);
    let expected = load_document(expected_path);

    assert_eq!(
        actual.stats().num_changes,
        expected.stats().num_changes,
        "{name}: changes"
    );
    assert_eq!(
        actual.stats().num_ops,
        expected.stats().num_ops,
        "{name}: operations"
    );
    assert_eq!(
        actual.stats().num_actors,
        expected.stats().num_actors,
        "{name}: actors"
    );
    assert_eq!(
        actual.hydrate(ROOT, None).unwrap(),
        expected.hydrate(ROOT, None).unwrap(),
        "{name}: visible state"
    );
}

fn main() {
    let arguments = env::args().skip(1).collect::<Vec<_>>();
    match arguments.as_slice() {
        [] => generate(),
        [command] if command == "generate" => generate(),
        [command, output_directory] if command == "verify-cpp" => {
            verify_cpp(Path::new(output_directory));
        }
        _ => panic!("usage: fixture-generator [generate | verify-cpp OUTPUT_DIRECTORY]"),
    }
}
